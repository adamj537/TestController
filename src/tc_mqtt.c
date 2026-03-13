/* tc_mqtt.c — MQTT client for G3 TC bringup shell
 *
 * Payload format: JSON matching mqtt-contract.md (authoritative).
 * Protocol:       MQTT 3.1.1, plain TCP (1883) for dev builds.
 *                 mTLS (8883) required in production — see prd-tc-connectivity-provisioning.md R4.1.6.
 * Topic structure: spBv1.0/SensitMfg/{type}/{serial}/CH{channel}
 *
 * NVS layout (per prd-tc-connectivity-provisioning.md):
 *   namespace "mqtt"   key "broker_url"  — e.g. "mqtt://10.0.0.1:1883"
 *   namespace "device" key "serial"      — e.g. "G3-MB-Tester-001"
 *   namespace "device" key "channel"     — uint8_t, 0-indexed
 *   namespace "device" key "bd_seq"      — uint64_t, monotonic, persisted
 */

#include "tc_mqtt.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_rom_crc.h"
#include "cmd_selftest.h"
#include "cmd_ota.h"
#include "cmd_swd.h"
#include "tc_statemachine.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "version.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "tc_mqtt";

/* ── Constants ────────────────────────────────────────────────────────────── */

#define BROKER_URL_LEN  128
#define SERIAL_LEN      32
#define TOPIC_LEN       80
#define NDEATH_MSG_LEN  48

/* ── Module state ─────────────────────────────────────────────────────────── */

static char     s_broker_url[BROKER_URL_LEN];
static char     s_serial[SERIAL_LEN];
static uint8_t  s_channel;
static uint64_t s_bd_seq;
static uint8_t  s_seq;
static bool     s_connected;
static bool     s_events_registered;

static esp_mqtt_client_handle_t s_client;

/* Static buffers for NDEATH last will — must outlive the MQTT client */
static char s_ndeath_topic[TOPIC_LEN];
static char s_ndeath_msg[NDEATH_MSG_LEN];

/* ── NVS helpers ──────────────────────────────────────────────────────────── */

static void nvs_load(void)
{
    nvs_handle_t h;

    if (nvs_open("mqtt", NVS_READONLY, &h) == ESP_OK) {
        size_t len = BROKER_URL_LEN;
        nvs_get_str(h, "broker_url", s_broker_url, &len);
        nvs_close(h);
    }

    if (nvs_open("device", NVS_READONLY, &h) == ESP_OK) {
        size_t len = SERIAL_LEN;
        nvs_get_str(h, "serial", s_serial, &len);
        nvs_get_u8(h, "channel", &s_channel);
        nvs_get_u64(h, "bd_seq", &s_bd_seq);
        nvs_close(h);
    }
}

static void nvs_save(void)
{
    nvs_handle_t h;

    if (nvs_open("mqtt", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "broker_url", s_broker_url);
        nvs_commit(h);
        nvs_close(h);
    }

    if (nvs_open("device", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "serial", s_serial);
        nvs_set_u8(h, "channel", s_channel);
        nvs_set_u64(h, "bd_seq", s_bd_seq);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ── Timestamp ────────────────────────────────────────────────────────────── */

static void get_iso8601(char *buf, size_t len)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    /* 1700000000 ≈ 2023-11-14: if clock is earlier, SNTP hasn't synced */
    if (tv.tv_sec < 1700000000L) {
        snprintf(buf, len, "1970-01-01T00:00:%02lu.%03luZ",
                 (unsigned long)(tv.tv_sec % 60),
                 (unsigned long)(tv.tv_usec / 1000));
        return;
    }

    struct tm tm;
    gmtime_r(&tv.tv_sec, &tm);
    snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec,
             (int)(tv.tv_usec / 1000));
}

/* ── Topic builder ────────────────────────────────────────────────────────── */

static void make_topic(char *buf, size_t len, const char *msg_type)
{
    snprintf(buf, len, "spBv1.0/SensitMfg/%s/%s/CH%u",
             msg_type, s_serial, (unsigned)s_channel);
}

/* ── NBIRTH ───────────────────────────────────────────────────────────────── */

static void publish_nbirth(void)
{
    char topic[TOPIC_LEN];
    make_topic(topic, sizeof(topic), "NBIRTH");

    /* WiFi RSSI */
    wifi_ap_record_t ap = {0};
    int32_t rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) rssi = ap.rssi;

    /* Unique identifier — WiFi STA MAC */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* SerialRef — CRC32 of MAC bytes, formatted XXXX-XXXX (9 chars) */
    uint32_t crc = esp_rom_crc32_le(0, mac, sizeof(mac));
    char serial_ref[10];
    snprintf(serial_ref, sizeof(serial_ref), "%04X-%04X",
             (unsigned)((crc >> 16) & 0xFFFF),
             (unsigned)(crc & 0xFFFF));

    char ts[32];
    get_iso8601(ts, sizeof(ts));

    char payload[1024];
    int n = snprintf(payload, sizeof(payload),
        "{"
        "\"bdSeq\":%llu,"
        "\"seq\":%u,"
        "\"timestamp\":\"%s\","
        "\"Node Control/Rebirth\":false,"
        "\"Properties/FirmwareVersion\":\"%s\","
        "\"Properties/HardwareRevision\":\"TCC-r1\","
        "\"Properties/SerialNumber\":\"%s\","
        "\"Properties/SerialRef\":\"%s\","
        "\"Properties/ChannelIndex\":%u,"
        "\"Properties/SchemaVersion\":\"2.0.0\","
        "\"Properties/RecipeVersion\":\"0.0.0\","
        "\"Properties/RecipeId\":\"none\","
        "\"Properties/CalProfileVersion\":\"none\","
        "\"Properties/CalExpiry\":\"none\","
        "\"State\":\"Idle\","
        "\"Diagnostics/WiFiRSSI\":%ld,"
        "\"Diagnostics/FreeHeap\":%lu,"
        "\"Diagnostics/DroppedMessages\":0"
        "}",
        (unsigned long long)s_bd_seq,
        (unsigned)s_seq,
        ts,
        FW_VERSION_STRING,
        mac_str,
        serial_ref,
        (unsigned)s_channel,
        (long)rssi,
        (unsigned long)esp_get_free_heap_size());

    if (n > 0 && n < (int)sizeof(payload)) {
        esp_mqtt_client_publish(s_client, topic, payload, n, 0, false);
        s_seq++;
        ESP_LOGI(TAG, "NBIRTH → %s  bdSeq=%llu", topic,
                 (unsigned long long)s_bd_seq);
    }

    /* Increment bdSeq for next session; persist */
    s_bd_seq++;
    nvs_save();
}

/* ── DCMD dispatch ────────────────────────────────────────────────────────── */

/* Minimal JSON string field extractor — no heap, no library.
 * Finds `"key":"value"` or `"key": value` and copies value into buf. */
static bool json_get_str(const char *json, const char *key, char *buf, size_t len)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    bool quoted = (*p == '"');
    if (quoted) p++;
    size_t i = 0;
    while (*p && i < len - 1) {
        if (quoted && *p == '"') break;
        if (!quoted && (*p == ',' || *p == '}' || *p == ' ')) break;
        buf[i++] = *p++;
    }
    buf[i] = '\0';
    return i > 0;
}

static void handle_dcmd(const char *payload, int len)
{
    char buf[256];
    int copy = len < (int)(sizeof(buf) - 1) ? len : (int)(sizeof(buf) - 1);
    memcpy(buf, payload, copy);
    buf[copy] = '\0';

    /* Sparkplug NCMD form: Node Control/Rebirth = true */
    if (strstr(buf, "Node Control/Rebirth") && strstr(buf, "true")) {
        ESP_LOGI(TAG, "DCMD: Node Control/Rebirth — triggering rebirth");
        goto do_rebirth;
    }

    char cmd[32] = {};
    if (!json_get_str(buf, "cmd", cmd, sizeof(cmd))) {
        ESP_LOGW(TAG, "DCMD: no 'cmd' field — ignoring: %s", buf);
        return;
    }

    if (strcmp(cmd, "rebirth") == 0) {
        ESP_LOGI(TAG, "DCMD: rebirth");
        goto do_rebirth;
    }

    if (strcmp(cmd, "selftest") == 0) {
        char mode[16] = "fixture";
        json_get_str(buf, "mode", mode, sizeof(mode));
        ESP_LOGI(TAG, "DCMD: selftest  mode=%s", mode);
        selftest_run_dcmd(mode);
        return;
    }

    if (strcmp(cmd, "start") == 0) {
        ESP_LOGI(TAG, "DCMD: start");
        tc_sm_cmd_start();
        return;
    }

    if (strcmp(cmd, "abort") == 0) {
        ESP_LOGI(TAG, "DCMD: abort");
        tc_sm_cmd_abort();
        return;
    }

    if (strcmp(cmd, "estop") == 0) {
        ESP_LOGW(TAG, "DCMD: estop — immediate stop");
        tc_sm_cmd_abort();
        return;
    }

    if (strcmp(cmd, "diagnostic") == 0) {
        char test[32] = {};
        if (!json_get_str(buf, "test", test, sizeof(test))) {
            ESP_LOGW(TAG, "DCMD: diagnostic — missing 'test' field");
            return;
        }
        ESP_LOGI(TAG, "DCMD: diagnostic  test=%s", test);
        selftest_run_diagnostic(test);
        return;
    }

    if (strcmp(cmd, "ota") == 0) {
        char url[256]    = {};
        char target[32]  = {};  /* "tc_firmware" (default) or "dut_firmware" */
        if (!json_get_str(buf, "url", url, sizeof(url))) {
            ESP_LOGW(TAG, "DCMD: ota — missing 'url' field");
            return;
        }
        json_get_str(buf, "target", target, sizeof(target));
        if (strcmp(target, "dut_firmware") == 0) {
            ESP_LOGI(TAG, "DCMD: ota  target=dut_firmware  url=%s", url);
            dut_fw_store_from_url(url);
        } else {
            ESP_LOGI(TAG, "DCMD: ota  target=tc_firmware  url=%s", url);
            ota_start_from_url(url);
        }
        return;
    }

    ESP_LOGW(TAG, "DCMD: unknown cmd '%s'", cmd);
    return;

do_rebirth:
    /* Re-publish NBIRTH: reset seq, keep bdSeq increment logic in publish_nbirth() */
    s_seq = 0;
    publish_nbirth();
}

/* ── MQTT event handler ───────────────────────────────────────────────────── */

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    (void)arg; (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED: {
            s_connected = true;
            s_seq = 0;
            publish_nbirth();

            /* Subscribe to DCMD after NBIRTH per Sparkplug B spec */
            char dcmd_topic[TOPIC_LEN];
            snprintf(dcmd_topic, sizeof(dcmd_topic),
                     "spBv1.0/SensitMfg/DCMD/%s/CH%u", s_serial, (unsigned)s_channel);
            int sub = esp_mqtt_client_subscribe(s_client, dcmd_topic, 1);
            ESP_LOGI(TAG, "DCMD subscribe → %s  msg_id=%d", dcmd_topic, sub);
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGI(TAG, "disconnected");
            break;
        case MQTT_EVENT_DATA:
            if (event->data && event->data_len > 0) {
                /* Only handle DCMD topics */
                if (event->topic_len > 0 &&
                    memmem(event->topic, event->topic_len, "/DCMD/", 6)) {
                    handle_dcmd(event->data, event->data_len);
                }
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error event");
            break;
        default:
            break;
    }
}

/* ── WiFi event handlers ──────────────────────────────────────────────────── */

static void on_got_ip(void *arg, esp_event_base_t base,
                      int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    if (s_client) esp_mqtt_client_start(s_client);
}

static void on_wifi_disconnect(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    s_connected = false;
    if (s_client) esp_mqtt_client_stop(s_client);
}

/* ── Client construction ──────────────────────────────────────────────────── */

static void build_and_register_client(void)
{
    if (s_client) {
        if (s_connected) {
            /* Publish explicit NDEATH before graceful disconnect (R2.4.4).
             * s_ndeath_topic/msg were set at client construction time with the
             * correct bdSeq for this session — publish them now before destroy. */
            esp_mqtt_client_publish(s_client, s_ndeath_topic, s_ndeath_msg,
                                    (int)strlen(s_ndeath_msg), 1, false);
            vTaskDelay(pdMS_TO_TICKS(100));   /* brief flush before destroy */
        }
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        s_connected = false;
    }

    /* Build NDEATH last will — bdSeq must match the upcoming NBIRTH */
    make_topic(s_ndeath_topic, sizeof(s_ndeath_topic), "NDEATH");
    snprintf(s_ndeath_msg, sizeof(s_ndeath_msg),
             "{\"bdSeq\":%llu}", (unsigned long long)s_bd_seq);

    char client_id[64];
    snprintf(client_id, sizeof(client_id), "G3-TC-%s-CH%u",
             s_serial, (unsigned)s_channel);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri              = s_broker_url,
        .credentials.client_id           = client_id,
        .session.keepalive               = 60,
        .session.disable_clean_session   = true,
        .session.last_will = {
            .topic   = s_ndeath_topic,
            .msg     = s_ndeath_msg,
            .msg_len = (int)strlen(s_ndeath_msg),
            .qos     = 1,
            .retain  = false,
        },
        .network.reconnect_timeout_ms = 5000,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);

    ESP_LOGI(TAG, "client created  broker=%s  id=%s", s_broker_url, client_id);
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void tc_mqtt_init(void)
{
    memset(s_broker_url, 0, sizeof(s_broker_url));
    strncpy(s_serial, "G3-MB-Tester-000", sizeof(s_serial) - 1);
    s_channel = 0;
    s_bd_seq  = 0;
    s_seq     = 0;
    s_connected = false;
    s_events_registered = false;
    s_client = NULL;

    nvs_load();

    ESP_LOGI(TAG, "init  broker=%s  serial=%s  ch=%u  bdSeq=%llu",
             s_broker_url[0] ? s_broker_url : "(none)",
             s_serial, (unsigned)s_channel,
             (unsigned long long)s_bd_seq);
}

void tc_mqtt_start(void)
{
    if (!s_broker_url[0]) {
        ESP_LOGI(TAG, "no broker configured — MQTT disabled");
        return;
    }

    build_and_register_client();
    if (!s_client) return;

    /* Register WiFi events once */
    if (!s_events_registered) {
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                   on_got_ip, NULL);
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                   on_wifi_disconnect, NULL);
        s_events_registered = true;
    }
}

bool tc_mqtt_connected(void)    { return s_connected; }
const char *tc_mqtt_serial(void) { return s_serial; }
uint8_t tc_mqtt_channel(void)   { return s_channel; }
const char *tc_mqtt_broker_url(void) { return s_broker_url; }

void tc_mqtt_configure(const char *broker_url, const char *serial,
                        uint8_t channel)
{
    strncpy(s_broker_url, broker_url, sizeof(s_broker_url) - 1);
    strncpy(s_serial,     serial,     sizeof(s_serial) - 1);
    s_channel = channel;
    nvs_save();

    /* Rebuild and start client with new config */
    build_and_register_client();
    if (s_client && !s_events_registered) {
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                   on_got_ip, NULL);
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                   on_wifi_disconnect, NULL);
        s_events_registered = true;
    }
    if (s_client) esp_mqtt_client_start(s_client);
}

void tc_mqtt_publish_selftest(const char *mode,
                               const tc_mqtt_check_t *checks,
                               int n_checks)
{
    if (!s_connected || !s_client) return;

    char ts[32];
    get_iso8601(ts, sizeof(ts));

    int pass_count = 0;
    for (int i = 0; i < n_checks; i++) {
        if (checks[i].pass) pass_count++;
    }
    bool all_pass = (pass_count == n_checks);

    char *buf = (char *)malloc(2048);
    if (!buf) return;

    int pos = snprintf(buf, 2048,
        "{"
        "\"type\":\"selftest\","
        "\"seq\":%u,"
        "\"timestamp\":\"%s\","
        "\"fixture_serial\":\"%s\","
        "\"channel\":%u,"
        "\"mode\":\"%s\","
        "\"outcome\":\"%s\","
        "\"checks\":[",
        (unsigned)s_seq, ts, s_serial, (unsigned)s_channel,
        mode, all_pass ? "pass" : "fail");

    for (int i = 0; i < n_checks && pos < 2000; i++) {
        if (i > 0 && pos < 2047) buf[pos++] = ',';
        if (checks[i].has_value) {
            pos += snprintf(buf + pos, 2048 - pos,
                "{\"id\":\"%s\",\"outcome\":\"%s\",\"measured\":%d}",
                checks[i].id,
                checks[i].pass ? "pass" : "fail",
                checks[i].value);
        } else {
            pos += snprintf(buf + pos, 2048 - pos,
                "{\"id\":\"%s\",\"outcome\":\"%s\"}",
                checks[i].id,
                checks[i].pass ? "pass" : "fail");
        }
    }

    if (pos < 2046) pos += snprintf(buf + pos, 2048 - pos, "]}");

    if (pos > 0 && pos < 2048) {
        char topic[TOPIC_LEN];
        make_topic(topic, sizeof(topic), "DDATA");
        int rc = esp_mqtt_client_publish(s_client, topic, buf, pos, 1, false);
        if (rc >= 0) {
            s_seq++;
            ESP_LOGI(TAG, "selftest DDATA → %s  outcome=%s  %d/%d checks  msg_id=%d",
                     topic, all_pass ? "pass" : "fail",
                     pass_count, n_checks, rc);
        } else {
            ESP_LOGW(TAG, "selftest DDATA publish failed  rc=%d", rc);
        }
    }

    free(buf);
}

void tc_mqtt_publish_state(const char *state)
{
    if (!s_connected || !s_client) return;

    char ts[32];
    get_iso8601(ts, sizeof(ts));

    char payload[256];
    int n = snprintf(payload, sizeof(payload),
        "{"
        "\"type\":\"state\","
        "\"seq\":%u,"
        "\"timestamp\":\"%s\","
        "\"fixture_serial\":\"%s\","
        "\"channel\":%u,"
        "\"state\":\"%s\""
        "}",
        (unsigned)s_seq, ts, s_serial, (unsigned)s_channel, state);

    if (n > 0 && n < (int)sizeof(payload)) {
        char topic[TOPIC_LEN];
        make_topic(topic, sizeof(topic), "DDATA");
        int rc = esp_mqtt_client_publish(s_client, topic, payload, n, 0, false);
        if (rc >= 0) {
            s_seq++;
            ESP_LOGI(TAG, "state DDATA → %s  state=%s", topic, state);
        } else {
            ESP_LOGW(TAG, "state DDATA publish failed  rc=%d", rc);
        }
    }
}

void tc_mqtt_publish_result(const char *outcome,
                             const char *failed_step,
                             uint32_t    duration_ms,
                             const char *dut_serial_full,
                             const char *dut_serial_ref,
                             const char *recipe_id,
                             const char *recipe_version)
{
    if (!s_connected || !s_client) return;

    char ts[32];
    get_iso8601(ts, sizeof(ts));

    /* failed_step is null for pass outcomes */
    char failed_step_json[64];
    if (failed_step && failed_step[0]) {
        snprintf(failed_step_json, sizeof(failed_step_json), "\"%s\"", failed_step);
    } else {
        snprintf(failed_step_json, sizeof(failed_step_json), "null");
    }

    char payload[512];
    int n = snprintf(payload, sizeof(payload),
        "{"
        "\"type\":\"result\","
        "\"seq\":%u,"
        "\"timestamp\":\"%s\","
        "\"fixture_serial\":\"%s\","
        "\"channel\":%u,"
        "\"dut_serial_full\":\"%s\","
        "\"dut_serial_ref\":\"%s\","
        "\"recipe_id\":\"%s\","
        "\"recipe_version\":\"%s\","
        "\"outcome\":\"%s\","
        "\"failed_step\":%s,"
        "\"duration_ms\":%lu,"
        "\"record_id\":null"
        "}",
        (unsigned)s_seq, ts, s_serial, (unsigned)s_channel,
        dut_serial_full ? dut_serial_full : "",
        dut_serial_ref  ? dut_serial_ref  : "",
        recipe_id       ? recipe_id       : "none",
        recipe_version  ? recipe_version  : "0.0.0",
        outcome,
        failed_step_json,
        (unsigned long)duration_ms);

    if (n > 0 && n < (int)sizeof(payload)) {
        char topic[TOPIC_LEN];
        make_topic(topic, sizeof(topic), "DDATA");
        /* QoS 1 per contract for result messages */
        int rc = esp_mqtt_client_publish(s_client, topic, payload, n, 1, false);
        if (rc >= 0) {
            s_seq++;
            ESP_LOGI(TAG, "result DDATA → %s  outcome=%s  duration=%lums  msg_id=%d",
                     topic, outcome, (unsigned long)duration_ms, rc);
        } else {
            ESP_LOGW(TAG, "result DDATA publish failed  rc=%d", rc);
        }
    }
}
