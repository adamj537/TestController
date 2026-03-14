#pragma once

#include <stdbool.h>
#include <stdint.h>

/* ── Test-cycle state machine ─────────────────────────────────────────────── *
 *
 * Normal cycle:
 *   Idle ──(start)──► Precheck ──(pass)──► [FlashDut] ──► Testing ──(pass)──► Pass
 *                         │                                    │                 │
 *                         └──(fail)──► Fail ◄──────────────────┘                │
 *                                        │                              (3s hold)│
 *                                        └─────────────────────────────────────► Idle
 *
 * Operator-initiated states (persist until cleared):
 *   FlashFail    — flash_dut step failed; green button clears → Idle
 *   SelftestFail — boot fixture selftest failed; green button clears → Idle
 *   Aborted      — operator red button press during running; green retries → Idle
 *   EStop        — hardware overcurrent or estop command; red button clears → Idle
 *
 * Connectivity overlay (not a state — overrides display while active):
 *   NoBroker flag — MQTT disconnected; display overrides; clears on reconnect
 *
 * MQTT state DDATA is published on every transition.
 * MQTT result DDATA is published on Pass/Fail/Abort/EStop.
 */

typedef enum {
    TC_SM_IDLE = 0,
    TC_SM_PRECHECK,
    TC_SM_FLASH_DUT,       /* downloading + programming DUT firmware via SWD */
    TC_SM_TESTING,
    TC_SM_PASS,
    TC_SM_FAIL,
    TC_SM_FLASH_FAIL,      /* DUT flash failed — green button clears */
    TC_SM_SELFTEST_FAIL,   /* fixture selftest failed at boot — green button clears */
    TC_SM_ABORTED,         /* test aborted by operator — green button retries */
    TC_SM_E_STOP,          /* emergency stop — red button or MQTT abort clears */
    TC_SM_STATE_COUNT,
} tc_sm_state_t;

/* Must be called once at startup (before tc_mqtt_start). */
void tc_sm_init(void);

/* Current state. */
tc_sm_state_t tc_sm_state(void);
const char   *tc_sm_state_str(void);

/* Called from DCMD/HMI button handler (tc_mqtt.c). */
void tc_sm_cmd_start(void);
void tc_sm_cmd_abort(void);
void tc_sm_cmd_estop(void);  /* raises E-STOP from any state */

/* Set firmware URL to download+flash before test execution.
 * Call before tc_sm_cmd_start(). Empty string disables the flash_dut step. */
void tc_sm_set_firmware_url(const char *url);

/* Called from MQTT connectivity events. */
void tc_sm_set_broker_connected(bool connected);

/* Called by the selftest task when a SM-owned run completes. */
void tc_sm_selftest_done(bool passed, uint32_t duration_ms);

/* Called by the flash_dut task when DUT programming completes.
 * ok:         true on success
 * error_code: PRD error code string on failure (e.g. "DOWNLOAD_FAIL"), or NULL on success
 * fw_size:    bytes programmed (0 on failure) */
void tc_sm_flash_done(bool ok, const char *error_code, uint32_t fw_size);

/* Called at boot if fixture selftest (quick) fails before first Idle entry. */
void tc_sm_selftest_fail_at_boot(void);
