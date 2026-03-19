/* g3_primitives.c — G3 Main Board recipe primitives.
 *
 * These are the recipe-layer primitives specific to the G3 Main Board DUT.
 * Names are transport-agnostic (power_check, dut_program, dut_read_id) — the
 * underlying mechanism (SWD, UART) is an implementation detail, not part of
 * the recipe interface.
 *
 * All functions match the primitive_fn_t signature:
 *   void fn(const cJSON *params)   — params may be NULL
 *
 * Params are read with cJSON_GetObjectItem(); missing keys fall back to
 * compiled-in defaults so recipes can omit fields they do not need to override.
 */

#include "recipe_primitives.h"
#include "cmd_selftest.h"
#include "cmd_vdac.h"
#include "cmd_i2c.h"
#include "cmd_swd.h"
#include "dut_identify.h"
#include "tc_config.h"
#include "tc_mqtt.h"
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "g3_prim";

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* Read an integer param with a fallback default. */
static int param_int(const cJSON *params, const char *key, int def)
{
    if (!params) return def;
    const cJSON *item = cJSON_GetObjectItem(params, key);
    return (item && cJSON_IsNumber(item)) ? (int)item->valuedouble : def;
}

/* ── INA219 register accessors (shared with cmd_selftest.c) ─────────────── */

#define ADDR_INA219_0       0x40
#define INA219_REG_BUS_V    0x02
#define INA219_REG_CURRENT  0x04
#define INA219_CURRENT_LSB_UA  10   /* µA per LSB — matches selftest_ina219_init() cal */

static bool ina219_read(uint8_t addr, int *vbus_mv_out, int *current_ma_out)
{
    uint8_t buf[2];

    /* Bus voltage: 13-bit, 4mV/LSB, bits[15:3] */
    if (!i2c_read_reg(addr, INA219_REG_BUS_V, buf, 2)) return false;
    int vbus_raw = ((buf[0] << 8) | buf[1]) >> 3;
    *vbus_mv_out = vbus_raw * 4;

    /* Current: signed 16-bit, Current_LSB = 10µA */
    if (!i2c_read_reg(addr, INA219_REG_CURRENT, buf, 2)) return false;
    int16_t current_raw = (int16_t)((buf[0] << 8) | buf[1]);
    *current_ma_out = (int)(current_raw * INA219_CURRENT_LSB_UA) / 1000;

    return true;
}

/* ── power_check ─────────────────────────────────────────────────────────── *
 *
 * Enable VDUT at the production operating point, measure DUT supply voltage
 * and quiescent current via INA219 #0, then disable VDUT.
 *
 * Params (all optional — limits fall back to device config, then hard defaults):
 *   "v_nominal_mv" : int  target VDUT voltage; duty derived from calibrated curve
 *                         in config (vdut.slope_mv_per_pct + vdut.intercept_mv).
 *                         Falls back to config "vdut.v_nominal_mv" (default 3300).
 *   "v_tolerance_pct": int  ± tolerance on v_nominal (default from config, 5%)
 *   "i_min_ma"     : int  minimum quiescent current (default from config)
 *   "i_max_ma"     : int  maximum quiescent current (default from config)
 *   "settle_ms"    : int  settling time after VDUT enable (default from config)
 *
 * Requires device config to be loaded and VDUT curve calibrated before use.
 * Fails with [FAIL] and no VDUT output if calibration is missing.
 */
void run_power_check(const cJSON *params)
{
    /* Resolve target voltage: recipe param → config → hard default */
    int v_nominal_mv = param_int(params, "v_nominal_mv",
                       tc_config_get_int("vdut.v_nominal_mv", 3300));

    /* Derive duty% from calibrated curve — fails if not calibrated */
    int duty_pct = tc_config_vdut_duty_for_mv(v_nominal_mv);
    if (duty_pct < 0) {
        printf("[FAIL] power_check: VDUT not calibrated — "
               "set vdut.slope_mv_per_pct and vdut.intercept_mv in config\n");
        selftest_check_record("power_v", false);
        selftest_check_record("power_i", false);
        return;
    }

    /* Voltage window from ±tolerance */
    int tol_pct   = param_int(params, "v_tolerance_pct",
                    tc_config_get_int("vdut.v_tolerance_pct", 5));
    int v_min_mv  = v_nominal_mv * (100 - tol_pct) / 100;
    int v_max_mv  = v_nominal_mv * (100 + tol_pct) / 100;

    int i_min_ma  = param_int(params, "i_min_ma",
                    tc_config_get_int("limits.i_idle_min_ma", 5));
    int i_max_ma  = param_int(params, "i_max_ma",
                    tc_config_get_int("limits.i_idle_max_ma", 250));
    int settle_ms = param_int(params, "settle_ms",
                    tc_config_get_int("vdut.settle_ms", 300));

    /* Enable VDUT at calibrated duty cycle.
     * Assert PB-A (ch0, SIG=0 active low) to latch DUT power — required alongside VDUT. */
    vdac_set_duty(0, duty_pct);  /* VDUT1 */
    vdac_set_duty(1, duty_pct);  /* VDUT2 */
    mux_select(0, 0);            /* PB-A assert: ch0, SIG=0 */
    vTaskDelay(pdMS_TO_TICKS(settle_ms));

    int vbus_mv = 0, current_ma = 0;
    bool read_ok = ina219_read(ADDR_INA219_0, &vbus_mv, &current_ma);

    /* Disable VDUT and release PB-A before pass/fail recording */
    mux_release();               /* SIG=1 — DUT KEEPALIVE must take over */
    vdac_set_duty(0, 0);
    vdac_set_duty(1, 0);

    if (!read_ok) {
        selftest_check_record("power_v", false);
        selftest_check_record("power_i", false);
        printf("[FAIL] power_check: INA219 read error\n");
        return;
    }

    bool v_ok = (vbus_mv >= v_min_mv && vbus_mv <= v_max_mv);
    bool i_ok = (current_ma >= i_min_ma && current_ma <= i_max_ma);

    printf("[%s] power_check: VDUT=%4d mV  (exp %d–%d mV)\n",
           v_ok ? "PASS" : "FAIL", vbus_mv, v_min_mv, v_max_mv);
    printf("[%s] power_check: I_DUT=%4d mA  (exp %d–%d mA)\n",
           i_ok ? "PASS" : "FAIL", current_ma, i_min_ma, i_max_ma);

    selftest_check_record_mv("power_v", v_ok, vbus_mv);
    selftest_check_record_mv("power_i", i_ok, current_ma);
}

/* ── dut_program ─────────────────────────────────────────────────────────── *
 *
 * Program DUT firmware from a previously stored partition image.
 * The transport (SWD) is an implementation detail — the recipe sees only
 * "program the DUT with the specified firmware slot".
 *
 * Params (all optional):
 *   "target"    : string  "pfw" (default) | "prod"
 *   "verify"    : bool    read-back verify after programming (default true)
 *   "timeout_s" : int     maximum seconds to allow (default 60)
 */
void run_dut_program(const cJSON *params)
{
    /* Resolve target */
    swd_fw_target_t target = SWD_FW_TARGET_PFW;
    if (params) {
        const cJSON *t = cJSON_GetObjectItem(params, "target");
        if (t && cJSON_IsString(t) && strcmp(t->valuestring, "prod") == 0)
            target = SWD_FW_TARGET_PROD;
    }

    bool verify    = true;
    int  timeout_s = 60;
    if (params) {
        const cJSON *v = cJSON_GetObjectItem(params, "verify");
        if (v && cJSON_IsBool(v)) verify = cJSON_IsTrue(v);
        timeout_s = param_int(params, "timeout_s", 60);
    }

    const char *target_str = (target == SWD_FW_TARGET_PFW) ? "pfw" : "prod";
    printf("dut_program: target=%s  verify=%s  timeout=%ds\n",
           target_str, verify ? "yes" : "no", timeout_s);

    uint32_t bytes_programmed = 0;
    swd_flash_err_t err = swd_flash_dut_local(verify, (uint32_t)timeout_s,
                                               &bytes_programmed, target);

    bool ok = (err == SWD_FLASH_OK);
    printf("[%s] dut_program: %s  %lu bytes  (%s)\n",
           ok ? "PASS" : "FAIL", target_str,
           (unsigned long)bytes_programmed, swd_flash_err_str(err));

    selftest_check_record("dut_program", ok);
}

/* ── dut_read_id ─────────────────────────────────────────────────────────── *
 *
 * Reset DUT, capture boot UART output, and extract a device identifier.
 * Records the identifier in the check buffer for MQTT DDATA inclusion.
 *
 * Params (all optional):
 *   "timeout_ms" : int  listen window in ms (default 3000)
 */
void run_dut_read_id(const cJSON *params)
{
    int timeout_ms = param_int(params, "timeout_ms", 3000);

    char id_buf[64] = {0};
    int id_len = dut_identify_uart(id_buf, sizeof(id_buf), timeout_ms);

    bool ok = (id_len > 0);
    if (ok) {
        printf("[PASS] dut_read_id: id=%s (%d chars)\n", id_buf, id_len);
        ESP_LOGI(TAG, "DUT id: %s", id_buf);
    } else {
        printf("[FAIL] dut_read_id: no identifier found within %d ms\n", timeout_ms);
    }

    selftest_check_record("dut_read_id", ok);
}
