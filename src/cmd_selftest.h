#pragma once

#include "tc_mqtt.h"  /* tc_mqtt_check_t */

#ifdef __cplusplus
extern "C" {
#endif

void register_selftest_commands(void);

/* Configure INA219 #0 and #1 with PGA=/1 and CAL=0xA000 (Current_LSB=10µA).
 * Must be called once at startup so current readings are valid without
 * running a selftest first. */
void selftest_ina219_init(void);

/* Run selftest from DCMD context.
 * mode: "fixture" (full) or "quick" (rail voltages + WiFi only).
 * Spawns a FreeRTOS task — returns immediately.
 * Publishes DDATA selftest result when complete. */
void selftest_run_dcmd(const char *mode);

/* Run selftest from state machine context.
 * Same as selftest_run_dcmd but notifies tc_sm_selftest_done() on completion. */
void selftest_run_sm(const char *mode);

/* Run a single named diagnostic test and publish a selftest DDATA.
 * test: "i2c" | "adc" | "wifi" | "ota" | "temp" | "vdut" | "mux" | "heartbeat"
 * Spawns a FreeRTOS task — returns immediately. */
void selftest_run_diagnostic(const char *test);

/* Returns the check ID string of the first failing check from the most recent
 * selftest run, or NULL if all checks passed.  Valid until the next run resets
 * the check buffer.  Safe to call from the selftest task before vTaskDelete. */
const char *selftest_first_failed_check(void);

/* ── Check buffer accessors (used by recipe_engine) ──────────────────────── */

/* Current check count and pass/total counters.
 * The engine snapshots ncheck before a step and reads new entries after. */
int selftest_get_ncheck(void);
int selftest_get_pass(void);
int selftest_get_total(void);
const tc_mqtt_check_t *selftest_get_checks(void);

/* Reset check buffer (call before recipe run). */
void selftest_reset_checks(void);

/* Record a check result into the check buffer.
 * Used by g3_primitives.c and any future primitive modules outside cmd_selftest.c. */
void selftest_check_record(const char *id, bool pass);
void selftest_check_record_mv(const char *id, bool pass, int mv);

#ifdef __cplusplus
}
#endif
