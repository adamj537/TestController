#pragma once

#include <stdbool.h>
#include <stdint.h>

/* ── Structured check entry for DDATA selftest ────────────────────────────── */

typedef struct {
    const char *id;        /* check ID string literal, e.g. "adc_rail_3v3" */
    bool        pass;
    bool        has_value; /* true when value field is meaningful */
    int         value;     /* generic measured value (mV, dBm, count, etc.) */
} tc_mqtt_check_t;

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

/* Read config from NVS.  Call once after nvs_flash_init(). */
void tc_mqtt_init(void);

/* Create MQTT client and register WiFi events.
 * No-op if broker URL not configured.  Call after wifi_init(). */
void tc_mqtt_start(void);

/* Returns true when broker connection is established and NBIRTH published */
bool tc_mqtt_connected(void);

/* Configure broker URL, fixture serial, channel index; persist to NVS.
 * Restarts the client if already running.
 * broker_url: e.g. "mqtt://10.0.0.1:1883"
 * serial:     e.g. "G3-MB-Tester-001"
 * channel:    0-indexed */
void tc_mqtt_configure(const char *broker_url, const char *serial, uint8_t channel);

/* Accessors */
const char *tc_mqtt_broker_url(void);
const char *tc_mqtt_serial(void);
uint8_t     tc_mqtt_channel(void);

/* ── DDATA publishers ─────────────────────────────────────────────────────── */

/* Publish DDATA type=state (QoS 0 per contract).
 * state: one of "Idle" | "Precheck" | "Loading" | "Programming" |
 *               "Testing" | "Pass" | "Fail" | "Fault" | "OTA" | "Diagnostic" */
void tc_mqtt_publish_state(const char *state);

/* Publish DDATA type=result (QoS 1 per contract).
 * outcome:         "pass" | "fail" | "aborted"
 * failed_step:     first failing step ID, or NULL/empty for pass
 * duration_ms:     test duration in milliseconds
 * dut_serial_full: full DUT UID hex string, or NULL
 * dut_serial_ref:  short CRC32 ref (XXXX-XXXX), or NULL
 * recipe_id:       active recipe ID string, or NULL
 * recipe_version:  active recipe semver, or NULL */
void tc_mqtt_publish_result(const char *outcome,
                             const char *failed_step,
                             uint32_t    duration_ms,
                             const char *dut_serial_full,
                             const char *dut_serial_ref,
                             const char *recipe_id,
                             const char *recipe_version);

/* Publish DDATA type=selftest (QoS 1 per contract).
 * mode:     "fixture" or "quick"
 * checks:   array of check entries (may be NULL if n_checks == 0)
 * n_checks: number of entries */
void tc_mqtt_publish_selftest(const char *mode,
                               const tc_mqtt_check_t *checks,
                               int n_checks);
