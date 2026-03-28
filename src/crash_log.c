/* crash_log.c — Lightweight crash persistence via esp_reset_reason() + NVS.
 *
 * No custom panic handler needed — we just check the reset reason register
 * at boot.  If the previous reset was abnormal, we save a record.
 *
 * Boot counter is persisted in NVS so each crash record has a unique ID.
 */

#include "crash_log.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_console.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "crash_log";

#define NVS_NS  "crash_log"
#define CAP     CRASH_LOG_CAPACITY

static uint8_t  s_head;
static uint8_t  s_count;
static uint32_t s_boot_count;

/* ── Reset reason helpers ────────────────────────────────────────────────── */

static const char *reason_to_str(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_POWERON:  return "POWERON";
        case ESP_RST_EXT:      return "EXT_RESET";
        case ESP_RST_SW:       return "SW_RESET";
        case ESP_RST_PANIC:    return "PANIC";
        case ESP_RST_INT_WDT:  return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT:      return "WDT_OTHER";
        case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO:     return "SDIO";
        default:               return "UNKNOWN";
    }
}

static bool is_abnormal(esp_reset_reason_t r)
{
    return r == ESP_RST_PANIC   ||
           r == ESP_RST_INT_WDT ||
           r == ESP_RST_TASK_WDT||
           r == ESP_RST_WDT     ||
           r == ESP_RST_BROWNOUT;
}

const char *crash_log_reset_reason_str(void)
{
    return reason_to_str(esp_reset_reason());
}

/* ── NVS helpers ─────────────────────────────────────────────────────────── */

static void make_key(char *buf, size_t len, uint8_t idx)
{
    snprintf(buf, len, "c%u", (unsigned)(idx % CAP));
}

static void save_pointers(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "head", s_head);
    nvs_set_u8(h, "cnt",  s_count);
    nvs_commit(h);
    nvs_close(h);
}

static void append_entry(const crash_log_entry_t *e)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    char key[4];
    make_key(key, sizeof(key), s_head);
    nvs_set_blob(h, key, e, sizeof(*e));
    nvs_commit(h);
    nvs_close(h);

    s_head = (s_head + 1) % CAP;
    if (s_count < CAP) s_count++;
    save_pointers();
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void crash_log_init(void)
{
    s_head  = 0;
    s_count = 0;
    s_boot_count = 0;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u8(h, "head", &s_head);
        nvs_get_u8(h, "cnt",  &s_count);
        nvs_get_u32(h, "boots", &s_boot_count);
        nvs_close(h);
    }

    s_head  = s_head % CAP;
    if (s_count > CAP) s_count = CAP;

    /* Increment and persist boot counter */
    s_boot_count++;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "boots", s_boot_count);
        nvs_commit(h);
        nvs_close(h);
    }

    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(TAG, "boot #%lu  reset_reason=%s", (unsigned long)s_boot_count,
             reason_to_str(reason));

    if (is_abnormal(reason)) {
        crash_log_entry_t e = {
            .reason       = (uint8_t)reason,
            .boot_count   = s_boot_count,
            .prev_uptime_s = 0,  /* unknown — would need RTC_NOINIT uptime tracking */
        };
        append_entry(&e);
        ESP_LOGW(TAG, "abnormal reset detected (%s) — saved to crash log",
                 reason_to_str(reason));
    }
}

void crash_log_dump_console(void)
{
    printf("crash_log: boot #%lu  last_reset=%s\n",
           (unsigned long)s_boot_count, reason_to_str(esp_reset_reason()));

    if (!s_count) {
        printf("  (no crash records)\n");
        return;
    }

    printf("  %u crash record%s:\n", s_count, s_count == 1 ? "" : "s");

    uint8_t oldest = (uint8_t)((s_head + CAP - s_count) % CAP);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        printf("  NVS open failed\n");
        return;
    }

    crash_log_entry_t e;
    for (uint8_t i = 0; i < s_count; i++) {
        char key[4];
        make_key(key, sizeof(key), (oldest + i) % CAP);
        size_t sz = sizeof(e);
        if (nvs_get_blob(h, key, &e, &sz) == ESP_OK) {
            printf("  [%u] boot #%lu  reason=%-10s",
                   (unsigned)i, (unsigned long)e.boot_count,
                   reason_to_str((esp_reset_reason_t)e.reason));
            if (e.prev_uptime_s > 0)
                printf("  uptime=%lus", (unsigned long)e.prev_uptime_s);
            printf("\n");
        }
    }
    nvs_close(h);
}

/* ── Console command ─────────────────────────────────────────────────────── */

static int do_crash(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
        s_head = 0;
        s_count = 0;
        save_pointers();
        printf("crash_log: cleared\n");
        return 0;
    }

    crash_log_dump_console();
    return 0;
}

void register_crash_log_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "crash",
        .help    = "crash [clear] — show or clear crash log",
        .hint    = NULL,
        .func    = &do_crash,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
