#pragma once

/* crash_log.h — Lightweight crash persistence via RTC_NOINIT + NVS.
 *
 * On panic/WDT reset the ESP32 reset reason register survives.
 * crash_log_init() reads esp_reset_reason() at boot and if the previous
 * reset was abnormal (panic, WDT, brownout) saves a record to NVS.
 *
 * NVS layout (namespace "crash_log"):
 *   key "head"  — uint8_t, next write slot
 *   key "cnt"   — uint8_t, valid entries
 *   key "c0".."c7" — blob (crash_log_entry_t)
 */

#include <stdint.h>
#include <stdbool.h>

#define CRASH_LOG_CAPACITY  8

typedef struct __attribute__((packed)) {
    uint8_t  reason;        /* esp_reset_reason_t value */
    uint32_t boot_count;    /* monotonic boot counter (NVS-persisted) */
    uint32_t prev_uptime_s; /* uptime of the session that crashed (0 if unknown) */
} crash_log_entry_t;

/* Call once after nvs_flash_init() and conn_log_init().
 * Checks reset reason; if abnormal, persists a crash record to NVS. */
void crash_log_init(void);

/* Print crash history to stdout. */
void crash_log_dump_console(void);

/* Current boot's reset reason as a string. */
const char *crash_log_reset_reason_str(void);

/* Register 'crash' console commands. */
void register_crash_log_commands(void);
