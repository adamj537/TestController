#pragma once

#include <stdbool.h>
#include <stdint.h>

/* ── Test-cycle state machine ─────────────────────────────────────────────── *
 *
 * States:
 *   Idle  ──(start)──►  Precheck  ──(pass)──►  Testing  ──(pass)──►  Pass
 *                           │                      │                    │
 *                           └──(fail)──►  Fail  ◄──┘                   │
 *                                          │                            │
 *                                          └──────────────────────►  Idle
 *                                                (after 3 s)
 *
 * An abort() from any non-Idle state transitions immediately to Idle.
 *
 * MQTT state DDATA is published on every transition.
 * MQTT result DDATA is published on Pass/Fail/Abort.
 */

typedef enum {
    TC_SM_IDLE = 0,
    TC_SM_PRECHECK,
    TC_SM_TESTING,
    TC_SM_PASS,
    TC_SM_FAIL,
} tc_sm_state_t;

/* Must be called once at startup (before tc_mqtt_start). */
void tc_sm_init(void);

/* Current state. */
tc_sm_state_t tc_sm_state(void);
const char   *tc_sm_state_str(void);

/* Called from DCMD handler (tc_mqtt.c). */
void tc_sm_cmd_start(void);
void tc_sm_cmd_abort(void);

/* Called by the selftest task when a SM-owned run completes.
 * passed:      true if all checks passed
 * duration_ms: wall-clock ms for that phase */
void tc_sm_selftest_done(bool passed, uint32_t duration_ms);
