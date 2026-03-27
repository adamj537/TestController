/* pre_test_gate.c — F5 pre-test gate (FLT-012)
 *
 * Runs between PRECHECK and FLASH_DUT/TESTING.  Verifies the DUT is correctly
 * inserted and that PFW is loaded before committing to a test cycle.
 *
 * Gate sequence:
 *   1. Preset VDUT voltage (LEDC configured on PWM GPIO), then enable + assert PB-A
 *   2. Wait for DUT power-up and INA219 settle
 *   3. INA219 current draw: min threshold (open-circuit guard) + max (short guard)
 *   4. SWD UID96 read — identifies DUT; failure = absent or unpowered
 *   5. Firmware descriptor at 0x08000200 — magic 0xC0DEBABE present → PFW loaded
 *      (Product FW statistically cannot match; pfw_loaded=false is not a fail)
 *
 * On pass: VDUT left on; DUT power held via PB-A for subsequent FLASH/TESTING.
 * On fail: VDUT disabled; PB-A released; SM transitions to FAIL → IDLE.
 *
 * Thresholds (NVS config keys, set via "config set"):
 *   pre_gate.current_min_ma  — minimum expected current draw (default 5 mA)
 *   pre_gate.current_max_ma  — maximum expected current draw (default 600 mA)
 *   pre_gate.vdut_mv         — DUT supply voltage to apply (default 3300 mV)
 */

#include "tc_statemachine.h"
#include "tc_config.h"
#include "tc_cal.h"
#include "cmd_swd.h"
#include "cmd_i2c.h"
#include "cmd_vdac.h"
#include "dut_detect.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static const char *TAG = "pre_gate";

/* PFW identity magic — must match the value written at 0x08000200 in dut-firmware */
#define PFW_MAGIC         0xC0DEBABEUL

/* INA219 #0 — measures VDUT1 current. Address and register layout from selftest. */
#define PRE_GATE_INA219_ADDR     0x40
#define INA219_REG_CURRENT       0x04
#define INA219_CURRENT_LSB_UA    10     /* µA per LSB (CAL=0xA000, PGA=/1) */

/* PB-A: U8 mux ch0, SIG=0 (active low).  Asserted via mux_select(). */
#define MUX_PBA_CH    0
#define MUX_PBA_SIG   0   /* active low */

/* ── INA219 current read helper ──────────────────────────────────────────── *
 * Reads the signed 16-bit CURRENT register; converts to mA.                 *
 * INA219 must already be configured (selftest_ina219_init at boot).          */
static bool read_ina219_current_ma(int *current_ma_out)
{
    uint8_t buf[2];
    if (!i2c_read_reg(PRE_GATE_INA219_ADDR, INA219_REG_CURRENT, buf, 2))
        return false;
    int16_t raw = (int16_t)((buf[0] << 8) | buf[1]);
    int raw_ma = (int)(raw * INA219_CURRENT_LSB_UA) / 1000;
    *current_ma_out = tc_cal_apply_ina_i(TC_CAL_INA_CH0, raw_ma);
    return true;
}

/* ── Gate task ───────────────────────────────────────────────────────────── */

static void pre_gate_task(void *pvarg)
{
    (void)pvarg;

    bool passed     = false;
    bool pfw_loaded = false;
    char uid_full[25] = "";
    char fail_reason[32] = "pre_gate";

    /* ── Step 0: Wait for dut_detect to fully stop ───────────────────────── */
    /* dut_detect_stop() is called by SM on off_idle, but the task may still be
     * mid-sample (mux_select + ADC128 read + mux_release).  If we assert PB-A
     * before it finishes, dut_detect's mux_release() undoes our PB-A. */
    dut_detect_wait_stopped();

    /* ── Step 1: Enable VDUT (calibrated) + assert PB-A ─────────────────── */
    /* Set voltage BEFORE enable: vdac_set_voltage() configures LEDC on the PWM
     * GPIO (gpio_reset_pin + ledc_channel_config).  If enable is asserted first,
     * the MP2315 starts with the FB pin floating → regulates to max → protection
     * fires → 0 mA output.  Match the SWD flash sequence: preset duty → enable. */
    int vdut_mv = tc_config_get_int("pre_gate.vdut_mv", 3300);
    ESP_LOGI(TAG, "enabling VDUT %d mV + PB-A", vdut_mv);
    if (!vdac_set_voltage(0, vdut_mv)) {
        ESP_LOGE(TAG, "VDUT calibration missing (slope=0) — gate cannot run");
        strlcpy(fail_reason, "vdut_uncalibrated", sizeof(fail_reason));
        goto done;
    }
    vdac_set_enable(0, true);
    mux_select(MUX_PBA_CH, MUX_PBA_SIG);   /* PB-A assert: ch0, SIG=0 */

    /* Wait for DUT to power up and INA219 conversion to settle */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* ── Step 2: INA219 current gate ─────────────────────────────────────── */
    int current_ma = 0;
    if (!read_ina219_current_ma(&current_ma)) {
        ESP_LOGE(TAG, "INA219 read failed");
        strlcpy(fail_reason, "ina219_read_fail", sizeof(fail_reason));
        goto power_off;
    }

    int min_ma = tc_config_get_int("pre_gate.current_min_ma", 5);
    int max_ma = tc_config_get_int("pre_gate.current_max_ma", 600);
    ESP_LOGI(TAG, "INA219: current=%d mA  (min=%d max=%d)", current_ma, min_ma, max_ma);

    if (current_ma < min_ma) {
        ESP_LOGW(TAG, "current too low — open circuit or DUT not seated");
        strlcpy(fail_reason, "current_low", sizeof(fail_reason));
        goto power_off;
    }
    if (current_ma > max_ma) {
        ESP_LOGE(TAG, "current too high — short or incorrect DUT");
        strlcpy(fail_reason, "current_high", sizeof(fail_reason));
        goto power_off;
    }

    /* ── Step 3: SWD UID96 read ──────────────────────────────────────────── */
    int uid_len = swd_read_dut_uid(uid_full, sizeof(uid_full));
    if (uid_len == 0) {
        ESP_LOGW(TAG, "SWD UID read failed — DUT absent or SWD fault");
        strlcpy(fail_reason, "swd_uid_fail", sizeof(fail_reason));
        goto power_off;
    }
    ESP_LOGI(TAG, "SWD UID: %s", uid_full);

    /* ── Step 4: Firmware descriptor at 0x08000200 ───────────────────────── */
    uint32_t descriptor = 0;
    int rc = swd_read_mem32(0x08000200, &descriptor);
    if (rc == 0 && descriptor == PFW_MAGIC) {
        pfw_loaded = true;
        ESP_LOGI(TAG, "PFW magic: 0x%08" PRIx32 " — PFW confirmed", descriptor);
    } else {
        ESP_LOGI(TAG, "PFW magic: 0x%08" PRIx32 " (rc=%d) — Product FW assumed",
                 descriptor, rc);
    }

    /* Gate passed — leave VDUT on and PB-A asserted for FLASH/TESTING */
    passed = true;
    goto done;

power_off:
    /* Gate failed — cut power */
    vdac_set_enable(0, false);
    mux_release();

done:
    tc_sm_pre_gate_done(passed, uid_full[0] ? uid_full : NULL, pfw_loaded,
                        passed ? NULL : fail_reason);
    vTaskDelete(NULL);
}

/* ── Public API (extern bridge from tc_statemachine.c) ───────────────────── */

void tc_sm_spawn_pre_gate_task(void)
{
    /* 8 KB stack: SWD bit-bang + HTTP-free; tc_config adds small JSON overhead */
    xTaskCreate(pre_gate_task, "pre_gate", 8192, NULL, 5, NULL);
}
