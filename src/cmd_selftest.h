#pragma once

#include <stddef.h>
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

/* Return the PFW version string cached by the most recent successful
 * run_dut_version() call.  Empty string if not yet read or last read failed.
 * Format: "x.y.z+N" (FW_VERSION_STRING from PFW build). */
void selftest_get_pfw_version(char *buf, size_t len);

/* Record a check result into the check buffer.
 * Used by g3_primitives.c and any future primitive modules outside cmd_selftest.c. */
void selftest_check_record(const char *id, bool pass);
void selftest_check_record_mv(const char *id, bool pass, int mv);

/* ── DUT UART command transport (shared with g3_primitives.c) ──────────── */

/* Open/close UART1 to DUT (115200 8N1).  Opened by run_dut_enter_test,
 * closed by run_dut_exit_test.  Thread-safe: no-op if already in target state. */
bool dut_uart_open(void);
void dut_uart_close(void);

/* Returns true if DUT UART is currently open (for guard checks in primitives). */
bool dut_uart_is_open(void);

/* Send "cmd\r\n", read one response line.  Returns true if response starts
 * with "OK".  buf receives the full response line (stripped of \r\n). */
bool dut_cmd(const char *cmd, char *buf, size_t buf_len, int timeout_ms);

/* ── TIE analog MUX + ADC128 read helpers (shared with g3_primitives.c) ── */

/* Select 1-of-16 channel on TIE MUX mux_idx (0–3).
 * MUX0→ADC128 CH0, MUX1→CH1, MUX2→CH2, MUX3→CH3. */
void tie_mux_select(int mux_idx, int ch);

/* Read raw ADC128D818 channel (0–7) in mV (2560 mV full-scale, 12-bit). */
bool tie_adc128_read_raw_mv(uint8_t ch, int *mv_out);

/* Initialise ADC128D818 for continuous high-rate conversion.  Call before
 * tie_adc128_read_raw_mv(); idempotent.  Returns true on success. */
bool tie_adc128_init(void);

#ifdef __cplusplus
}
#endif
