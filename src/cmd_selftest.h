#pragma once

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

#ifdef __cplusplus
}
#endif
