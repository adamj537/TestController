#include "tc_statemachine.h"
#include "tc_mqtt.h"
#include "cmd_selftest.h"
#include "cmd_swd.h"
#include "display_strings.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "tc_sm";

/* ── State ────────────────────────────────────────────────────────────────── */

static tc_sm_state_t s_state     = TC_SM_IDLE;
static uint64_t      s_start_us  = 0;   /* time when start() was called */
static bool          s_sm_owned  = false; /* true when SM spawned the running selftest */

/* DUT serial captured during identify step */
static char s_dut_serial_full[64];
static char s_dut_serial_ref[10];   /* XXXX-XXXX */

static const char *const s_state_names[] = {
    "Idle", "Precheck", "Testing", "Pass", "Fail"
};

/* ── Internal helpers ─────────────────────────────────────────────────────── */

static void transition(tc_sm_state_t next)
{
    ESP_LOGI(TAG, "%s → %s", s_state_names[s_state], s_state_names[next]);
    s_state = next;
    tc_mqtt_publish_state(s_state_names[next]);
}

static uint32_t elapsed_ms(void)
{
    return (uint32_t)((esp_timer_get_time() - s_start_us) / 1000ULL);
}

/* Read DUT unique identifier via SWD (STM32 UID96 at 0x1FFF7590).
 * This is the primary DUT identifier — always available regardless of
 * what firmware is loaded on the DUT.  No UART cooperation required. */
static void identify_dut(void)
{
    s_dut_serial_full[0] = '\0';
    s_dut_serial_ref[0]  = '\0';

    int len = swd_read_dut_uid(s_dut_serial_full, sizeof(s_dut_serial_full));
    if (len <= 0) {
        ESP_LOGW(TAG, "DUT serial: SWD UID read failed");
        return;
    }

    /* Derive short ref: CRC32 of the full UID, formatted as XXXX-XXXX */
    uint32_t crc = esp_rom_crc32_le(0, (const uint8_t *)s_dut_serial_full,
                                    (uint32_t)len);
    snprintf(s_dut_serial_ref, sizeof(s_dut_serial_ref), "%04X-%04X",
             (unsigned)((crc >> 16) & 0xFFFFu),
             (unsigned)(crc         & 0xFFFFu));

    ESP_LOGI(TAG, "DUT UID: full=%s  ref=%s", s_dut_serial_full, s_dut_serial_ref);
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void tc_sm_init(void)
{
    s_state    = TC_SM_IDLE;
    s_sm_owned = false;
    s_dut_serial_full[0] = '\0';
    s_dut_serial_ref[0]  = '\0';
}

tc_sm_state_t tc_sm_state(void)
{
    return s_state;
}

const char *tc_sm_state_str(void)
{
    return s_state_names[s_state];
}

void tc_sm_cmd_start(void)
{
    if (s_state != TC_SM_IDLE) {
        ESP_LOGW(TAG, "start ignored — already in state=%s", s_state_names[s_state]);
        return;
    }

    s_start_us = esp_timer_get_time();
    s_sm_owned = true;
    s_dut_serial_full[0] = '\0';
    s_dut_serial_ref[0]  = '\0';

    transition(TC_SM_PRECHECK);
    /* Quick selftest as precheck — rail voltages + WiFi */
    selftest_run_sm("quick");
}

void tc_sm_cmd_abort(void)
{
    if (s_state == TC_SM_IDLE) return;

    ESP_LOGW(TAG, "abort from state=%s", s_state_names[s_state]);
    s_sm_owned = false;

    uint32_t ms = elapsed_ms();
    transition(TC_SM_IDLE);
    tc_mqtt_publish_result("aborted", NULL, ms,
                           s_dut_serial_full[0] ? s_dut_serial_full : NULL,
                           s_dut_serial_ref[0]  ? s_dut_serial_ref  : NULL,
                           NULL, NULL);
}

/* ── Called by selftest task ──────────────────────────────────────────────── *
 * NOTE: this runs in the selftest FreeRTOS task context, so vTaskDelay
 * is safe here.  The task calls vTaskDelete(NULL) after this returns.
 */
void tc_sm_selftest_done(bool passed, uint32_t duration_ms)
{
    if (!s_sm_owned) {
        /* Selftest was triggered manually (shell cmd), not by state machine. */
        return;
    }

    if (s_state == TC_SM_PRECHECK) {
        if (passed) {
            /* Precheck OK — attempt DUT serial identification before Testing */
            /* HW-010 fix: switched PSRAM from OPI to Quad mode, freeing
             * GPIO 37 (SPIDQS) for SWD SWCLK.  Next TCC rev should move
             * SWCLK off GPIO 33–37 so OPI can be restored if needed. */
            identify_dut();

            transition(TC_SM_TESTING);
            selftest_run_sm("fixture");
            /* selftest_run_sm spawns a new task; this task's job is done */
        } else {
            /* Precheck failed — report and return to Idle */
            s_sm_owned = false;
            uint32_t total_ms = elapsed_ms();
            transition(TC_SM_FAIL);
            tc_mqtt_publish_result("fail", "precheck", total_ms,
                                   NULL, NULL, NULL, NULL);
            vTaskDelay(pdMS_TO_TICKS(DISP_HOLD_FAIL_MS));
            transition(TC_SM_IDLE);
        }
    } else if (s_state == TC_SM_TESTING) {
        s_sm_owned = false;
        uint32_t total_ms = elapsed_ms();

        const char *serial_full = s_dut_serial_full[0] ? s_dut_serial_full : NULL;
        const char *serial_ref  = s_dut_serial_ref[0]  ? s_dut_serial_ref  : NULL;

        if (passed) {
            transition(TC_SM_PASS);
            ESP_LOGI(TAG, "publishing result DDATA  outcome=pass  duration=%lums",
                     (unsigned long)total_ms);
            tc_mqtt_publish_result("pass", NULL, total_ms,
                                   serial_full, serial_ref,
                                   "fixture-selftest", "0.0.0");
        } else {
            /* Use actual failing check ID from the selftest run, not a generic label */
            const char *failed = selftest_first_failed_check();
            transition(TC_SM_FAIL);
            ESP_LOGI(TAG, "publishing result DDATA  outcome=fail  failed=%s  duration=%lums",
                     failed ? failed : "(null)", (unsigned long)total_ms);
            tc_mqtt_publish_result("fail", failed ? failed : "fixture_selftest", total_ms,
                                   serial_full, serial_ref,
                                   "fixture-selftest", "0.0.0");
        }

        /* Allow MQTT outbound queue to flush, then hold display per approved strings */
        vTaskDelay(pdMS_TO_TICKS(500));
        vTaskDelay(pdMS_TO_TICKS(passed ? DISP_HOLD_PASS_MS : DISP_HOLD_FAIL_MS));
        transition(TC_SM_IDLE);
    } else {
        /* Unexpected — could happen if abort raced the selftest completion */
        ESP_LOGW(TAG, "selftest_done in unexpected state=%s — ignoring",
                 s_state_names[s_state]);
        s_sm_owned = false;
    }
}
