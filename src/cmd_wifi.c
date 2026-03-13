#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/ip4_addr.h"
#include "esp_netif_sntp.h"
#include "cmd_wifi.h"

static const char *TAG = "wifi";

#define NVS_NS       "wifi_cfg"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "pass"

#define CONNECTED_BIT BIT0
#define FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_events;

/* ── NVS helpers ──────────────────────────────────────────────────────────── */

static esp_err_t cred_save(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_str(h, NVS_KEY_SSID, ssid);
    nvs_set_str(h, NVS_KEY_PASS, pass);
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static bool cred_load(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = (nvs_get_str(h, NVS_KEY_SSID, ssid, &ssid_len) == ESP_OK &&
               nvs_get_str(h, NVS_KEY_PASS, pass, &pass_len) == ESP_OK &&
               ssid[0] != '\0');
    nvs_close(h);
    return ok;
}

/* ── Event handler ────────────────────────────────────────────────────────── */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        /* WiFi hardware is ready — safe to connect now */
        char ssid[33] = {}, pass[65] = {};
        if (cred_load(ssid, sizeof(ssid), pass, sizeof(pass))) {
            ESP_LOGI(TAG, "Auto-connecting to '%s'", ssid);
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)data;
        ESP_LOGI(TAG, "Disconnected (reason %d)", ev->reason);
        xEventGroupSetBits(s_wifi_events, FAIL_BIT);
        /* Attempt to reconnect automatically */
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        printf("WiFi connected — IP: " IPSTR "\n", IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(s_wifi_events, CONNECTED_BIT);

        /* Start SNTP once — survives reconnects */
        static bool s_sntp_started = false;
        if (!s_sntp_started) {
            esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            esp_netif_sntp_init(&sntp_cfg);
            s_sntp_started = true;
        }
    }
}

/* ── Commands ─────────────────────────────────────────────────────────────── */

static int do_wifi_scan(int argc, char **argv)
{
    (void)argc; (void)argv;

    wifi_scan_config_t cfg = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&cfg, true);   /* blocking */
    if (err != ESP_OK) {
        printf("Scan failed: %s\n", esp_err_to_name(err));
        return 1;
    }

    uint16_t count = 20;
    wifi_ap_record_t records[20];
    esp_wifi_scan_get_ap_records(&count, records);

    printf("%-32s  CH  RSSI\n", "SSID");
    printf("%-32s  --  ----\n", "--------------------------------");
    for (uint16_t i = 0; i < count; i++) {
        printf("%-32s  %2d  %4d\n",
               (char *)records[i].ssid,
               records[i].primary,
               records[i].rssi);
    }
    printf("\n%d AP(s) found\n", count);
    return 0;
}

static int do_wifi_connect(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: wifi connect <ssid> <password>\n");
        return 1;
    }

    const char *ssid = argv[1];
    const char *pass = argv[2];

    /* Persist credentials so auto-connect works after reboot / OTA */
    if (cred_save(ssid, pass) != ESP_OK) {
        printf("Warning: failed to save credentials to NVS\n");
    }

    wifi_config_t wcfg = {};
    strlcpy((char *)wcfg.sta.ssid,     ssid, sizeof(wcfg.sta.ssid));
    strlcpy((char *)wcfg.sta.password, pass, sizeof(wcfg.sta.password));

    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &wcfg);

    xEventGroupClearBits(s_wifi_events, CONNECTED_BIT | FAIL_BIT);
    esp_wifi_connect();

    printf("Connecting to '%s'...\n", ssid);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                           CONNECTED_BIT | FAIL_BIT,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(10000));
    if (bits & CONNECTED_BIT) return 0;   /* IP printed in event handler */
    printf("Connection failed\n");
    return 1;
}

static int do_wifi_disconnect(int argc, char **argv)
{
    (void)argc; (void)argv;
    esp_wifi_disconnect();
    printf("Disconnected\n");
    return 0;
}

static int do_wifi_status(int argc, char **argv)
{
    (void)argc; (void)argv;

    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        printf("SSID    : %s\n", (char *)ap.ssid);
        printf("RSSI    : %d dBm\n", ap.rssi);
        printf("Channel : %d\n", ap.primary);

        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_ip_info_t ip;
            if (esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
                printf("IP      : " IPSTR "\n", IP2STR(&ip.ip));
                printf("GW      : " IPSTR "\n", IP2STR(&ip.gw));
            }
        }
    } else {
        printf("Not connected\n");
        char ssid[33] = {};
        size_t len = sizeof(ssid);
        nvs_handle_t h;
        if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
            if (nvs_get_str(h, NVS_KEY_SSID, ssid, &len) == ESP_OK && ssid[0]) {
                printf("Saved   : %s\n", ssid);
            }
            nvs_close(h);
        }
    }
    return 0;
}

static int do_wifi_forget(int argc, char **argv)
{
    (void)argc; (void)argv;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY_SSID);
        nvs_erase_key(h, NVS_KEY_PASS);
        nvs_commit(h);
        nvs_close(h);
    }
    esp_wifi_disconnect();
    printf("Credentials cleared\n");
    return 0;
}

static int do_wifi(int argc, char **argv)
{
    if (argc < 2) {
        printf("WiFi commands:\n");
        printf("  wifi scan                    scan for APs\n");
        printf("  wifi connect <ssid> <pass>   connect and save credentials\n");
        printf("  wifi disconnect              disconnect from AP\n");
        printf("  wifi status                  show connection status and IP\n");
        printf("  wifi forget                  clear saved credentials\n");
        return 1;
    }
    if (strcmp(argv[1], "scan")       == 0) return do_wifi_scan      (argc-1, argv+1);
    if (strcmp(argv[1], "connect")    == 0) return do_wifi_connect   (argc-1, argv+1);
    if (strcmp(argv[1], "disconnect") == 0) return do_wifi_disconnect(argc-1, argv+1);
    if (strcmp(argv[1], "status")     == 0) return do_wifi_status    (argc-1, argv+1);
    if (strcmp(argv[1], "forget")     == 0) return do_wifi_forget    (argc-1, argv+1);
    printf("Unknown subcommand '%s'\n", argv[1]);
    return 1;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void wifi_init(void)
{
    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_register(WIFI_EVENT,  ESP_EVENT_ANY_ID,    &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT,    IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    /* Pre-load saved credentials so they are set when WIFI_EVENT_STA_START fires */
    char ssid[33] = {}, pass[65] = {};
    if (cred_load(ssid, sizeof(ssid), pass, sizeof(pass))) {
        wifi_config_t wcfg = {};
        strlcpy((char *)wcfg.sta.ssid,     ssid, sizeof(wcfg.sta.ssid));
        strlcpy((char *)wcfg.sta.password, pass, sizeof(wcfg.sta.password));
        esp_wifi_set_config(WIFI_IF_STA, &wcfg);
        /* Connection is triggered in the STA_START event handler — not here */
    }

    ESP_ERROR_CHECK(esp_wifi_start());
}

void register_wifi_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "wifi",
        .help    = "WiFi: wifi <scan|connect|disconnect|status|forget>",
        .hint    = NULL,
        .func    = &do_wifi,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
