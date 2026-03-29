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
#include "tc_cal.h"
#include "tc_mqtt.h"
#include "meas_log.h"
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "g3_prim";

/* ── Snapshot storage ────────────────────────────────────────────────────── */

#define SNAP_NAME_LEN  16
#define MUX_SNAP_MAX    8
#define LTC_SNAP_MAX    8
#define LTC_CH_COUNT   16

typedef struct {
    char name[SNAP_NAME_LEN];
    int  mv[4][16];   /* [mux_idx][channel] */
    bool valid;
} mux_snapshot_t;

typedef struct {
    char name[SNAP_NAME_LEN];
    int  mv[LTC_CH_COUNT];  /* -1 = read error in baseline */
    bool valid;
} ltc_snapshot_t;

static mux_snapshot_t s_mux_snaps[MUX_SNAP_MAX];
static ltc_snapshot_t s_ltc_snaps[LTC_SNAP_MAX];

/* Set true when PC5 (LTC2498 VREF_ENA) is driven HIGH via dut_gpio_set.
 * Cleared on PC5 LOW.  dut_peripheral_adc_read auto-re-enables if false. */
static bool s_ltc2498_enabled = false;

static mux_snapshot_t *mux_snap_find_or_alloc(const char *name)
{
    for (int i = 0; i < MUX_SNAP_MAX; i++) {
        if (!s_mux_snaps[i].valid || strcmp(s_mux_snaps[i].name, name) == 0) {
            strlcpy(s_mux_snaps[i].name, name, SNAP_NAME_LEN);
            s_mux_snaps[i].valid = true;
            return &s_mux_snaps[i];
        }
    }
    return NULL;
}

static const mux_snapshot_t *mux_snap_find(const char *name)
{
    for (int i = 0; i < MUX_SNAP_MAX; i++) {
        if (s_mux_snaps[i].valid && strcmp(s_mux_snaps[i].name, name) == 0)
            return &s_mux_snaps[i];
    }
    return NULL;
}

static ltc_snapshot_t *ltc_snap_find_or_alloc(const char *name)
{
    for (int i = 0; i < LTC_SNAP_MAX; i++) {
        if (!s_ltc_snaps[i].valid || strcmp(s_ltc_snaps[i].name, name) == 0) {
            strlcpy(s_ltc_snaps[i].name, name, SNAP_NAME_LEN);
            s_ltc_snaps[i].valid = true;
            return &s_ltc_snaps[i];
        }
    }
    return NULL;
}

static const ltc_snapshot_t *ltc_snap_find(const char *name)
{
    for (int i = 0; i < LTC_SNAP_MAX; i++) {
        if (s_ltc_snaps[i].valid && strcmp(s_ltc_snaps[i].name, name) == 0)
            return &s_ltc_snaps[i];
    }
    return NULL;
}

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
    int ch = (addr == ADDR_INA219_0) ? TC_CAL_INA_CH0 : TC_CAL_INA_CH1;

    /* Bus voltage: 13-bit, 4mV/LSB, bits[15:3] */
    if (!i2c_read_reg(addr, INA219_REG_BUS_V, buf, 2)) return false;
    int vbus_raw = ((buf[0] << 8) | buf[1]) >> 3;
    *vbus_mv_out = tc_cal_apply_ina_v(ch, vbus_raw * 4);

    /* Current: signed 16-bit, Current_LSB = 10µA */
    if (!i2c_read_reg(addr, INA219_REG_CURRENT, buf, 2)) return false;
    int16_t current_raw = (int16_t)((buf[0] << 8) | buf[1]);
    int raw_ma = (int)(current_raw * INA219_CURRENT_LSB_UA) / 1000;
    *current_ma_out = tc_cal_apply_ina_i(ch, raw_ma);

    return true;
}

/* Read VDUT1 current (INA219 #0) only — used by DUT removal watchdog.
 * Returns false on I2C error; *ma_out unchanged on error. */
bool g3_ina219_read_current_ma(int *ma_out)
{
    int vbus_mv = 0, current_ma = 0;
    if (!ina219_read(ADDR_INA219_0, &vbus_mv, &current_ma)) return false;
    *ma_out = current_ma;
    return true;
}

/* ── g3_critical_abort ───────────────────────────────────────────────────── *
 *
 * Called by the recipe engine on a CRITICAL step failure.
 * Cuts DUT power immediately (VDUT1+2 off, PB-A released).
 * Must complete within the engine's 100 ms abort budget.
 */
void g3_critical_abort(void)
{
    u8_mux_release();     /* Release PB-A (SIG=1) immediately */
    vdac_disable(0);   /* VDUT1 off */
    vdac_disable(1);   /* VDUT2 off */
    printf("[ABORT] CRITICAL step failure — DUT power cut\n");
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

    /* Enable VDUT1 at calibrated voltage only if not already on.
     * Calling vdac_set_voltage() while the regulator is running calls
     * gpio_reset_pin() on the feedback GPIO, which briefly floats the
     * MP2315 feedback pin and can trip its over-voltage protection.
     * Pre-gate and dut_program leave VDUT1 on at the correct setpoint. */
    if (!vdac_is_enabled(0)) {
        if (!vdac_set_voltage(0, v_nominal_mv)) {
            printf("[FAIL] power_check: VDUT1 not calibrated — "
                   "set vdut.slope_mv_per_pct and vdut.intercept_mv in config\n");
            selftest_check_record("power_v", false);
            selftest_check_record("power_i", false);
            return;
        }
        vdac_set_enable(0, true);
    }
    u8_mux_select(0, 0);            /* PB-A assert: ch0, SIG=0 */
    vTaskDelay(pdMS_TO_TICKS(settle_ms));

    int vbus_mv = 0, current_ma = 0;
    bool read_ok = ina219_read(ADDR_INA219_0, &vbus_mv, &current_ma);

    /* Release PB-A — DUT KEEPALIVE must hold power.
     * VDUT stays on for subsequent steps (e.g. dut_heartbeat). */
    u8_mux_release();   /* SIG=1 — DUT KEEPALIVE must take over */

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

    /* Measurement log — branches[] for DDATA v2.15.0. */
    meas_log_record(&(meas_entry_t){
        .branch    = "System",
        .connector = "",
        .net_id    = "VDUT1_bus",
        .name      = "Bus Voltage",
        .scenario  = '\0',
        .measured  = (float)vbus_mv,
        .unit      = "mV",
        .limit_min = (float)v_min_mv,
        .limit_max = (float)v_max_mv,
        .soft_limit_min = NAN,
        .soft_limit_max = NAN,
        .verdict   = v_ok,
    });
    meas_log_record(&(meas_entry_t){
        .branch    = "System",
        .connector = "",
        .net_id    = "VDUT1_current",
        .name      = "Quiescent Current",
        .scenario  = '\0',
        .measured  = (float)current_ma,
        .unit      = "mA",
        .limit_min = (float)i_min_ma,
        .limit_max = (float)i_max_ma,
        .soft_limit_min = NAN,
        .soft_limit_max = NAN,
        .verdict   = i_ok,
    });
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

/* ── swd_probe ─────────────────────────────────────────────────────────────── *
 * Minimal SWD connect + IDCODE check.  DUT must already be powered.
 * Verifies pogo contact to SWD pins (SWDIO/SWCLK) by reading IDCODE.
 *
 * Params:
 *   "check_id" : string  check ID for result recording (default "swd_probe")
 */
void run_swd_probe(const cJSON *params)
{
    const char *check_id = "swd_probe";
    if (params) {
        const cJSON *item = cJSON_GetObjectItem(params, "check_id");
        if (item && cJSON_IsString(item)) check_id = item->valuestring;
    }

    bool ok = swd_probe_dut();
    if (ok) {
        printf("[PASS] %s: STM32L476 IDCODE 0x2BA01477\n", check_id);
    } else {
        printf("[FAIL] %s: SWD connect failed — check pogo contacts\n", check_id);
    }
    selftest_check_record(check_id, ok);
}

/* ── Param helpers ─────────────────────────────────────────────────────────── */

static const char *param_str(const cJSON *params, const char *key, const char *def)
{
    if (!params) return def;
    const cJSON *item = cJSON_GetObjectItem(params, key);
    return (item && cJSON_IsString(item)) ? item->valuestring : def;
}

/* ── mux_read ──────────────────────────────────────────────────────────────── *
 * Read a specific TIE MUX channel via ADC128D818 and assert the voltage
 * is within a specified range.
 *
 * Params:
 *   "mux"     : int  MUX index (0–3)
 *   "ch"      : int  MUX channel (0–15)
 *   "min_mv"  : int  low threshold in mV (default 0)
 *   "max_mv"  : int  high threshold in mV (default 3300)
 *   "settle_ms": int settle time after mux select (default 20)
 *   "check_id": string  check ID for result recording (default "mux_read")
 */
void run_mux_read(const cJSON *params)
{
    int mux_idx   = param_int(params, "mux", 0);
    int ch        = param_int(params, "ch", 0);
    int min_mv    = param_int(params, "min_mv", 0);
    int max_mv    = param_int(params, "max_mv", 3300);
    int settle_ms = param_int(params, "settle_ms", 20);
    const char *check_id = param_str(params, "check_id", "mux_read");

    if (mux_idx < 0 || mux_idx > 3 || ch < 0 || ch > 15) {
        printf("[FAIL] mux_read: invalid mux=%d ch=%d\n", mux_idx, ch);
        selftest_check_record(check_id, false);
        return;
    }

    if (!tie_adc128_init()) {
        printf("[FAIL] mux_read: ADC128 init failed\n");
        selftest_check_record(check_id, false);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(150));

    tie_mux_select(mux_idx, ch);
    vTaskDelay(pdMS_TO_TICKS(settle_ms));

    int mv = 0;
    bool read_ok = tie_adc128_read_raw_mv((uint8_t)mux_idx, &mv);
    if (!read_ok) {
        printf("[FAIL] mux_read: ADC128 read error  mux=%d ch=%d\n", mux_idx, ch);
        selftest_check_record(check_id, false);
        return;
    }

    bool in_range = (mv >= min_mv && mv <= max_mv);
    printf("[%s] mux_read: mux=%d ch=%d  %d mV  (exp %d–%d mV)\n",
           in_range ? "PASS" : "FAIL", mux_idx, ch, mv, min_mv, max_mv);
    selftest_check_record_mv(check_id, in_range, mv);
}

/* ── dut_gpio_set ──────────────────────────────────────────────────────────── *
 * Send GPIO_SET <pin> <HIGH|LOW> to DUT via UART.
 *
 * Params:
 *   "pin"   : string  GPIO pin name (e.g. "PA9", "PE5")
 *   "level" : string  "HIGH" or "LOW" (default "HIGH")
 */
void run_dut_gpio_set(const cJSON *params)
{
    const char *pin   = param_str(params, "pin", NULL);
    const char *level = param_str(params, "level", "HIGH");

    if (!pin) {
        printf("[FAIL] dut_gpio_set: missing 'pin' param\n");
        selftest_check_record("dut_gpio_set", false);
        return;
    }
    if (!dut_uart_is_open()) {
        printf("[FAIL] dut_gpio_set: UART not open\n");
        selftest_check_record("dut_gpio_set", false);
        return;
    }

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "GPIO_SET %s %s", pin, level);
    char resp[128] = {0};
    bool ok = dut_cmd(cmd, resp, sizeof(resp), 500);
    printf("[%s] dut_gpio_set: %s  %s\n", ok ? "PASS" : "FAIL", cmd, resp);
    selftest_check_record("dut_gpio_set", ok);

    /* Track LTC2498 VREF enable state for auto-re-enable guard */
    if (ok && strcmp(pin, "PC5") == 0)
        s_ltc2498_enabled = (strcmp(level, "HIGH") == 0);
}

/* ── dut_gpio_clear ────────────────────────────────────────────────────────── *
 * Send GPIO_CLEAR <pin> to DUT via UART (returns pin to hi-Z input).
 *
 * Params:
 *   "pin" : string  GPIO pin name
 */
void run_dut_gpio_clear(const cJSON *params)
{
    const char *pin = param_str(params, "pin", NULL);
    if (!pin) {
        printf("[FAIL] dut_gpio_clear: missing 'pin' param\n");
        selftest_check_record("dut_gpio_clear", false);
        return;
    }
    if (!dut_uart_is_open()) {
        printf("[FAIL] dut_gpio_clear: UART not open\n");
        selftest_check_record("dut_gpio_clear", false);
        return;
    }

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "GPIO_CLEAR %s", pin);
    char resp[128] = {0};
    bool ok = dut_cmd(cmd, resp, sizeof(resp), 500);
    printf("[%s] dut_gpio_clear: %s  %s\n", ok ? "PASS" : "FAIL", cmd, resp);
    selftest_check_record("dut_gpio_clear", ok);
}

/* ── dut_pin_read ──────────────────────────────────────────────────────────── *
 * Send PIN_READ <pin> to DUT, assert expected level.
 *
 * Params:
 *   "pin"      : string  GPIO pin name
 *   "expected" : string  "HIGH" or "LOW" (default "HIGH")
 *   "check_id" : string  check ID (default "dut_pin_read")
 */
void run_dut_pin_read(const cJSON *params)
{
    const char *pin      = param_str(params, "pin", NULL);
    const char *expected = param_str(params, "expected", "HIGH");
    const char *check_id = param_str(params, "check_id", "dut_pin_read");

    if (!pin) {
        printf("[FAIL] dut_pin_read: missing 'pin' param\n");
        selftest_check_record(check_id, false);
        return;
    }
    if (!dut_uart_is_open()) {
        printf("[FAIL] dut_pin_read: UART not open\n");
        selftest_check_record(check_id, false);
        return;
    }

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "PIN_READ %s", pin);
    char resp[128] = {0};
    bool ok = dut_cmd(cmd, resp, sizeof(resp), 500);

    /* Response: "OK PIN_READ <pin> HIGH" or "OK PIN_READ <pin> LOW" */
    bool level_ok = false;
    if (ok) {
        level_ok = (strstr(resp, expected) != NULL);
    }

    printf("[%s] dut_pin_read: %s  %s  (exp %s)\n",
           (ok && level_ok) ? "PASS" : "FAIL", pin, resp, expected);
    selftest_check_record(check_id, ok && level_ok);
}

/* ── dut_uc_adc_read ───────────────────────────────────────────────────────── *
 * Send UC_ADC_READ <channel> to DUT, assert mV within range.
 * Generic version — works with any UC ADC channel name.
 *
 * Params:
 *   "channel"  : string  ADC channel name (e.g. "VREF", "3V_RAIL")
 *   "min_mv"   : int     low threshold (default 0)
 *   "max_mv"   : int     high threshold (default 3300)
 *   "check_id" : string  check ID (default "dut_uc_adc")
 */
void run_dut_uc_adc_read(const cJSON *params)
{
    const char *channel  = param_str(params, "channel", "VREF");
    int min_mv           = param_int(params, "min_mv", 0);
    int max_mv           = param_int(params, "max_mv", 3300);
    const char *check_id = param_str(params, "check_id", "dut_uc_adc");

    if (!dut_uart_is_open()) {
        printf("[FAIL] dut_uc_adc_read: UART not open\n");
        selftest_check_record(check_id, false);
        return;
    }

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "UC_ADC_READ %s", channel);
    char resp[128] = {0};
    bool ok = dut_cmd(cmd, resp, sizeof(resp), 500);

    int mv = 0;
    if (ok) {
        /* Response: "OK UC_ADC_READ <channel> <mv>" */
        char *last_space = strrchr(resp, ' ');
        if (last_space) mv = atoi(last_space + 1);
    }

    bool in_range = ok && (mv >= min_mv && mv <= max_mv);
    printf("[%s] dut_uc_adc_read: %s  %d mV  (exp %d–%d mV)\n",
           in_range ? "PASS" : "FAIL", channel, mv, min_mv, max_mv);
    selftest_check_record_mv(check_id, in_range, mv);

    /* Measurement log — branches[] for DDATA v2.15.0. */
    {
        meas_entry_t e = {0};
        strlcpy(e.branch, "DUT_ADC", sizeof(e.branch));
        snprintf(e.net_id, sizeof(e.net_id), "uc_%s", channel);
        snprintf(e.name,   sizeof(e.name),   "UC ADC %s", channel);
        e.measured  = (float)mv;
        strlcpy(e.unit, "mV", sizeof(e.unit));
        e.limit_min = (float)min_mv;
        e.limit_max = (float)max_mv;
        e.soft_limit_min = NAN;
        e.soft_limit_max = NAN;
        e.verdict   = in_range;
        meas_log_record(&e);
    }
}

/* ── dut_peripheral_adc_read ───────────────────────────────────────────────── *
 * Send PERIPHERAL_ADC_READ <channel> to DUT (LTC2498), assert mV within range.
 * Uses longer timeout due to 160 ms delta-sigma conversion.
 *
 * Params:
 *   "channel"  : string  ADC24 channel name (e.g. "VMON1", "CMON1", "VBATT")
 *   "min_mv"   : int     low threshold (default 0)
 *   "max_mv"   : int     high threshold (default 5000)
 *   "check_id" : string  check ID (default "dut_peri_adc")
 */
void run_dut_peripheral_adc_read(const cJSON *params)
{
    const char *channel  = param_str(params, "channel", "VMON1");
    int min_mv           = param_int(params, "min_mv", 0);
    int max_mv           = param_int(params, "max_mv", 5000);
    const char *check_id = param_str(params, "check_id", "dut_peri_adc");

    if (!dut_uart_is_open()) {
        printf("[FAIL] dut_peripheral_adc_read: UART not open\n");
        selftest_check_record(check_id, false);
        return;
    }

    /* Auto-re-enable LTC2498 VREF if it was cleared (e.g. power cycle) */
    if (!s_ltc2498_enabled) {
        char re_resp[64] = {0};
        bool re_ok = dut_cmd("GPIO_SET PC5 HIGH", re_resp, sizeof(re_resp), 500);
        if (re_ok) {
            s_ltc2498_enabled = true;
            vTaskDelay(pdMS_TO_TICKS(50));  /* brief settle */
            ESP_LOGW(TAG, "dut_peripheral_adc_read: auto-re-enabled PC5 (VREF)");
        }
    }

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "PERIPHERAL_ADC_READ %s", channel);
    char resp[128] = {0};
    /* 500ms timeout: 160ms conversion + margin */
    bool ok = dut_cmd(cmd, resp, sizeof(resp), 500);

    int mv = 0;
    if (ok) {
        /* Response: "OK PERIPHERAL_ADC_READ <channel> <mv> mV" */
        const char *p = strstr(resp, channel);
        if (p) {
            p += strlen(channel);
            while (*p == ' ') p++;
            mv = atoi(p);
        }
    }

    bool in_range = ok && (mv >= min_mv && mv <= max_mv);
    printf("[%s] dut_peripheral_adc_read: %s  %d mV  (exp %d–%d mV)\n",
           in_range ? "PASS" : "FAIL", channel, mv, min_mv, max_mv);
    selftest_check_record_mv(check_id, in_range, mv);
}

/* ── dut_pwr_enable / dut_pwr_disable ──────────────────────────────────────── *
 * Send PWR_ENABLE <branch> or PWR_DISABLE <branch> to DUT.
 *
 * Params:
 *   "branch"   : int     branch number (1–6)
 *   "check_id" : string  check ID (default "dut_pwr")
 */
void run_dut_pwr_enable(const cJSON *params)
{
    int branch           = param_int(params, "branch", 1);
    const char *check_id = param_str(params, "check_id", "dut_pwr");

    if (!dut_uart_is_open()) {
        printf("[FAIL] dut_pwr_enable: UART not open\n");
        selftest_check_record(check_id, false);
        return;
    }

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "PWR_ENABLE %d", branch);
    char resp[128] = {0};
    bool ok = dut_cmd(cmd, resp, sizeof(resp), 500);
    printf("[%s] dut_pwr_enable: branch %d  %s\n",
           ok ? "PASS" : "FAIL", branch, resp);
    selftest_check_record(check_id, ok);
}

void run_dut_pwr_disable(const cJSON *params)
{
    int branch           = param_int(params, "branch", 1);
    const char *check_id = param_str(params, "check_id", "dut_pwr");

    if (!dut_uart_is_open()) {
        printf("[FAIL] dut_pwr_disable: UART not open\n");
        selftest_check_record(check_id, false);
        return;
    }

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "PWR_DISABLE %d", branch);
    char resp[128] = {0};
    bool ok = dut_cmd(cmd, resp, sizeof(resp), 500);
    printf("[%s] dut_pwr_disable: branch %d  %s\n",
           ok ? "PASS" : "FAIL", branch, resp);
    selftest_check_record(check_id, ok);
}

/* ── dut_i2c_scan ──────────────────────────────────────────────────────────── *
 * Send I2C_SCAN <bus> to DUT, assert expected addresses are present.
 *
 * Params:
 *   "bus"      : int     I2C bus number (1–3)
 *   "expected" : string  comma-separated hex addresses (e.g. "0x48,0x50")
 *   "check_id" : string  check ID (default "dut_i2c_scan")
 */
void run_dut_i2c_scan(const cJSON *params)
{
    int bus              = param_int(params, "bus", 1);
    const char *expected = param_str(params, "expected", "");
    const char *check_id = param_str(params, "check_id", "dut_i2c_scan");

    if (!dut_uart_is_open()) {
        printf("[FAIL] dut_i2c_scan: UART not open\n");
        selftest_check_record(check_id, false);
        return;
    }

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "I2C_SCAN %d", bus);
    char resp[128] = {0};
    bool ok = dut_cmd(cmd, resp, sizeof(resp), 2000);

    if (!ok) {
        printf("[FAIL] dut_i2c_scan: bus %d  %s\n", bus, resp);
        selftest_check_record(check_id, false);
        return;
    }

    /* Response: "OK I2C_SCAN <bus> ACK: 0xNN 0xNN ..." or "OK I2C_SCAN <bus> NONE" */
    bool all_found = true;
    if (expected[0] != '\0') {
        /* Parse expected addresses and verify each appears in response */
        char exp_copy[128];
        strncpy(exp_copy, expected, sizeof(exp_copy) - 1);
        exp_copy[sizeof(exp_copy) - 1] = '\0';

        char *tok = strtok(exp_copy, ",");
        while (tok) {
            /* Strip whitespace */
            while (*tok == ' ') tok++;
            if (strstr(resp, tok) == NULL) {
                printf("[FAIL] dut_i2c_scan: bus %d  expected %s not found\n", bus, tok);
                all_found = false;
            }
            tok = strtok(NULL, ",");
        }
    }

    printf("[%s] dut_i2c_scan: bus %d  %s\n",
           (ok && all_found) ? "PASS" : "FAIL", bus, resp);
    selftest_check_record(check_id, ok && all_found);
}

/* ── branch_test ───────────────────────────────────────────────────────────── *
 * Composite primitive: enable DUT branch → settle → read V/I via INA219 or
 * ADC24 → verify within limits → disable branch.
 *
 * Params:
 *   "branch"   : int     branch number to enable (1–6)
 *   "v_channel": string  ADC24 channel name for voltage (e.g. "VMON1")
 *   "i_channel": string  ADC24 channel name for current (e.g. "CMON1")
 *   "v_min_mv" : int     voltage low threshold (default 0)
 *   "v_max_mv" : int     voltage high threshold (default 5500)
 *   "i_min_mv" : int     current low threshold in 0.01mA units (default 0)
 *   "i_max_mv" : int     current high threshold (default 500)
 *   "settle_ms": int     settling time after enable (default 200)
 *   "check_id" : string  base check ID (appended with _v and _i)
 */
void run_branch_test(const cJSON *params)
{
    int branch           = param_int(params, "branch", 1);
    const char *v_ch     = param_str(params, "v_channel", "VMON1");
    const char *i_ch     = param_str(params, "i_channel", "CMON1");
    int v_min_mv         = param_int(params, "v_min_mv", 0);
    int v_max_mv         = param_int(params, "v_max_mv", 5500);
    int i_min_mv         = param_int(params, "i_min_mv", 0);
    int i_max_mv         = param_int(params, "i_max_mv", 500);
    int settle_ms        = param_int(params, "settle_ms", 200);
    const char *check_id = param_str(params, "check_id", "branch");

    if (!dut_uart_is_open()) {
        printf("[FAIL] branch_test: UART not open\n");
        char id_v[64], id_i[64];
        snprintf(id_v, sizeof(id_v), "%s_v", check_id);
        snprintf(id_i, sizeof(id_i), "%s_i", check_id);
        selftest_check_record(id_v, false);
        selftest_check_record(id_i, false);
        return;
    }

    /* Enable branch */
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "PWR_ENABLE %d", branch);
    char resp[128] = {0};
    bool en_ok = dut_cmd(cmd, resp, sizeof(resp), 500);
    if (!en_ok) {
        printf("[FAIL] branch_test: PWR_ENABLE %d failed  %s\n", branch, resp);
        char id_v[64], id_i[64];
        snprintf(id_v, sizeof(id_v), "%s_v", check_id);
        snprintf(id_i, sizeof(id_i), "%s_i", check_id);
        selftest_check_record(id_v, false);
        selftest_check_record(id_i, false);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(settle_ms));

    /* Read voltage via LTC2498 */
    snprintf(cmd, sizeof(cmd), "PERIPHERAL_ADC_READ %s", v_ch);
    bool v_ok = dut_cmd(cmd, resp, sizeof(resp), 500);
    int v_mv = 0;
    if (v_ok) {
        const char *p = strstr(resp, v_ch);
        if (p) {
            p += strlen(v_ch);
            while (*p == ' ') p++;
            v_mv = atoi(p);
        }
    }

    /* Read current via LTC2498 */
    snprintf(cmd, sizeof(cmd), "PERIPHERAL_ADC_READ %s", i_ch);
    bool i_ok = dut_cmd(cmd, resp, sizeof(resp), 500);
    int i_mv = 0;
    if (i_ok) {
        const char *p = strstr(resp, i_ch);
        if (p) {
            p += strlen(i_ch);
            while (*p == ' ') p++;
            i_mv = atoi(p);
        }
    }

    /* Disable branch */
    snprintf(cmd, sizeof(cmd), "PWR_DISABLE %d", branch);
    dut_cmd(cmd, resp, sizeof(resp), 500);

    /* Record results */
    bool v_in_range = v_ok && (v_mv >= v_min_mv && v_mv <= v_max_mv);
    bool i_in_range = i_ok && (i_mv >= i_min_mv && i_mv <= i_max_mv);

    char id_v[64], id_i[64];
    snprintf(id_v, sizeof(id_v), "%s_v", check_id);
    snprintf(id_i, sizeof(id_i), "%s_i", check_id);

    printf("[%s] branch_test: br%d %s=%d mV  (exp %d–%d)\n",
           v_in_range ? "PASS" : "FAIL", branch, v_ch, v_mv, v_min_mv, v_max_mv);
    printf("[%s] branch_test: br%d %s=%d     (exp %d–%d)\n",
           i_in_range ? "PASS" : "FAIL", branch, i_ch, i_mv, i_min_mv, i_max_mv);

    selftest_check_record_mv(id_v, v_in_range, v_mv);
    selftest_check_record_mv(id_i, i_in_range, i_mv);
}

/* ── short_detect ──────────────────────────────────────────────────────────── *
 * Scan adjacent TIE MUX channels for unexpected coupling.
 * DUT GPIO drives one pin HIGH, then reads surrounding MUX channels —
 * any that exceed the threshold indicate a solder bridge or trace short.
 *
 * Params:
 *   "mux"       : int     MUX index (0–3)
 *   "ch_start"  : int     first channel to scan (default 0)
 *   "ch_end"    : int     last channel to scan (default 15)
 *   "skip_ch"   : int     channel to skip (the driven pin, default -1 = none)
 *   "thresh_mv" : int     max allowed voltage on non-driven channels (default 200)
 *   "check_id"  : string  check ID (default "short_detect")
 */
void run_short_detect(const cJSON *params)
{
    int mux_idx    = param_int(params, "mux", 0);
    int ch_start   = param_int(params, "ch_start", 0);
    int ch_end     = param_int(params, "ch_end", 15);
    int skip_ch    = param_int(params, "skip_ch", -1);
    int thresh_mv  = param_int(params, "thresh_mv", 200);
    const char *check_id = param_str(params, "check_id", "short_detect");

    if (mux_idx < 0 || mux_idx > 3) {
        printf("[FAIL] short_detect: invalid mux=%d\n", mux_idx);
        selftest_check_record(check_id, false);
        return;
    }

    if (!tie_adc128_init()) {
        printf("[FAIL] short_detect: ADC128 init failed\n");
        selftest_check_record(check_id, false);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(150));

    int short_count = 0;
    for (int ch = ch_start; ch <= ch_end && ch < 16; ch++) {
        if (ch == skip_ch) continue;

        tie_mux_select(mux_idx, ch);
        vTaskDelay(pdMS_TO_TICKS(20));

        int mv = 0;
        if (tie_adc128_read_raw_mv((uint8_t)mux_idx, &mv)) {
            if (mv > thresh_mv) {
                printf("[WARN] short_detect: mux=%d ch=%d  %d mV > %d mV threshold\n",
                       mux_idx, ch, mv, thresh_mv);
                short_count++;
            }
        }
    }

    bool pass = (short_count == 0);
    printf("[%s] short_detect: mux=%d  %d shorts detected\n",
           pass ? "PASS" : "FAIL", mux_idx, short_count);
    selftest_check_record(check_id, pass);
}

/* ── mux_snapshot ──────────────────────────────────────────────────────────── *
 * Read all 4 TIE MUXes × 16 channels and store as a named snapshot.
 * Used as a baseline before enabling a load; pair with mux_compare_snapshot.
 * No check recorded — this is a capture step.
 *
 * Params:
 *   "name" : string  snapshot name (max 15 chars, e.g. "pre_vref")
 */
void run_mux_snapshot(const cJSON *params)
{
    const char *name = param_str(params, "name", "snap");

    mux_snapshot_t *snap = mux_snap_find_or_alloc(name);
    if (!snap) {
        printf("[FAIL] mux_snapshot: snapshot table full\n");
        return;
    }
    snap->valid = false;

    if (!tie_adc128_init()) {
        printf("[FAIL] mux_snapshot: ADC128 init failed\n");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(150));

    int fail_count = 0;
    for (int mux_idx = 0; mux_idx < 4; mux_idx++) {
        for (int ch = 0; ch < 16; ch++) {
            tie_mux_select(mux_idx, ch);
            vTaskDelay(pdMS_TO_TICKS(20));
            int mv = 0;
            if (!tie_adc128_read_raw_mv((uint8_t)mux_idx, &mv)) {
                snap->mv[mux_idx][ch] = -1;
                fail_count++;
            } else {
                snap->mv[mux_idx][ch] = mv;
            }
        }
    }

    snap->valid = true;
    printf("[INFO] mux_snapshot: \"%s\" stored (%d read errors)\n", name, fail_count);
}

/* ── mux_compare_snapshot ──────────────────────────────────────────────────── *
 * Re-scan all 4×16 TIE channels and compare against a stored mux_snapshot.
 * Fails if any non-ignored channel delta exceeds noise_mv (absolute value).
 * Channels whose baseline was -1 (read error) are always skipped.
 *
 * Params:
 *   "name"     : string  snapshot to compare (default "snap")
 *   "noise_mv" : int     max allowed delta in mV (default 100)
 *   "check_id" : string  check ID (default "mux_xcheck")
 *   "ignore"   : array   [{mux: int, ch: int}, ...] channels to skip
 */
void run_mux_compare_snapshot(const cJSON *params)
{
    const char *name     = param_str(params, "name", "snap");
    int noise_mv         = param_int(params, "noise_mv", 100);
    const char *check_id = param_str(params, "check_id", "mux_xcheck");

    const mux_snapshot_t *baseline = mux_snap_find(name);
    if (!baseline || !baseline->valid) {
        printf("[FAIL] mux_compare_snapshot: snapshot \"%s\" not found\n", name);
        selftest_check_record(check_id, false);
        return;
    }

    const cJSON *ignore_arr = params ? cJSON_GetObjectItem(params, "ignore") : NULL;

    if (!tie_adc128_init()) {
        printf("[FAIL] mux_compare_snapshot: ADC128 init failed\n");
        selftest_check_record(check_id, false);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(150));

    int delta_count = 0;
    for (int mux_idx = 0; mux_idx < 4; mux_idx++) {
        for (int ch = 0; ch < 16; ch++) {
            int baseline_mv = baseline->mv[mux_idx][ch];
            if (baseline_mv < 0) continue;  /* baseline read error — skip */

            /* Check ignore list */
            bool skip = false;
            if (ignore_arr) {
                int arr_sz = cJSON_GetArraySize(ignore_arr);
                for (int k = 0; k < arr_sz && !skip; k++) {
                    const cJSON *item = cJSON_GetArrayItem(ignore_arr, k);
                    const cJSON *im = cJSON_GetObjectItem(item, "mux");
                    const cJSON *ic = cJSON_GetObjectItem(item, "ch");
                    if (im && cJSON_IsNumber(im) && ic && cJSON_IsNumber(ic) &&
                        (int)im->valuedouble == mux_idx && (int)ic->valuedouble == ch)
                        skip = true;
                }
            }
            if (skip) continue;

            tie_mux_select(mux_idx, ch);
            vTaskDelay(pdMS_TO_TICKS(20));

            int mv = 0;
            if (!tie_adc128_read_raw_mv((uint8_t)mux_idx, &mv)) continue;

            int delta = mv - baseline_mv;
            if (delta < 0) delta = -delta;
            if (delta > noise_mv) {
                printf("[WARN] mux_compare_snapshot: mux=%d ch=%d  delta=%d mV"
                       "  (was %d mV, now %d mV)\n",
                       mux_idx, ch, delta, baseline_mv, mv);
                delta_count++;
            }
        }
    }

    bool pass = (delta_count == 0);
    printf("[%s] mux_compare_snapshot: \"%s\"  %d unexpected deltas  (noise_mv=%d)\n",
           pass ? "PASS" : "FAIL", name, delta_count, noise_mv);
    selftest_check_record(check_id, pass);
}

/* ── ltc2498_snapshot ──────────────────────────────────────────────────────── *
 * Send PERIPHERAL_ADC_SCAN to DUT, parse all 16 LTC2498 channels, and store
 * as a named snapshot.  Requires DUT UART open.  If a channel returns
 * ADC_READ_ERROR it is stored as -1 and skipped in future comparisons.
 * No check recorded — this is a capture step.
 *
 * Params:
 *   "name" : string  snapshot name (default "ltc_snap")
 */
void run_ltc2498_snapshot(const cJSON *params)
{
    const char *name = param_str(params, "name", "ltc_snap");

    if (!dut_uart_is_open()) {
        printf("[FAIL] ltc2498_snapshot: UART not open\n");
        return;
    }

    ltc_snapshot_t *snap = ltc_snap_find_or_alloc(name);
    if (!snap) {
        printf("[FAIL] ltc2498_snapshot: snapshot table full\n");
        return;
    }
    snap->valid = false;

    /* ~160ms × 16 channels ≈ 2.56s; use 3.5s timeout */
    char buf[512] = {0};
    bool ok = dut_cmd_multiline("PERIPHERAL_ADC_SCAN", buf, sizeof(buf), 3500, "END");
    if (!ok) {
        printf("[FAIL] ltc2498_snapshot: PERIPHERAL_ADC_SCAN timeout or error\n");
        return;
    }

    /* Parse "  CH%02d  <value> mV"  or "  CH%02d  ADC_READ_ERROR" */
    for (int i = 0; i < LTC_CH_COUNT; i++) {
        char needle[6];
        snprintf(needle, sizeof(needle), "CH%02d", i);
        const char *p = strstr(buf, needle);
        if (!p) { snap->mv[i] = -1; continue; }
        p += 4;                      /* skip "CH%02d" */
        while (*p == ' ') p++;
        snap->mv[i] = ((*p >= '0' && *p <= '9') || *p == '-') ? (int)strtol(p, NULL, 10) : -1;
    }

    snap->valid = true;
    printf("[INFO] ltc2498_snapshot: \"%s\" stored\n", name);
}

/* ── ltc2498_compare_snapshot ──────────────────────────────────────────────── *
 * Re-scan all LTC2498 channels and compare against a stored ltc2498_snapshot.
 * Fails if any non-ignored channel delta exceeds noise_mv.
 * Channels with baseline == -1 or current == -1 (errors) are always skipped.
 *
 * Params:
 *   "name"     : string  snapshot to compare (default "ltc_snap")
 *   "noise_mv" : int     max allowed delta in mV (default 100)
 *   "check_id" : string  check ID (default "ltc_xcheck")
 *   "ignore"   : array   [int, ...] channel indices (0–15) to skip
 */
void run_ltc2498_compare_snapshot(const cJSON *params)
{
    const char *name     = param_str(params, "name", "ltc_snap");
    int noise_mv         = param_int(params, "noise_mv", 100);
    const char *check_id = param_str(params, "check_id", "ltc_xcheck");

    if (!dut_uart_is_open()) {
        printf("[FAIL] ltc2498_compare_snapshot: UART not open\n");
        selftest_check_record(check_id, false);
        return;
    }

    const ltc_snapshot_t *baseline = ltc_snap_find(name);
    if (!baseline || !baseline->valid) {
        printf("[FAIL] ltc2498_compare_snapshot: snapshot \"%s\" not found\n", name);
        selftest_check_record(check_id, false);
        return;
    }

    const cJSON *ignore_arr = params ? cJSON_GetObjectItem(params, "ignore") : NULL;

    char buf[512] = {0};
    bool ok = dut_cmd_multiline("PERIPHERAL_ADC_SCAN", buf, sizeof(buf), 3500, "END");
    if (!ok) {
        printf("[FAIL] ltc2498_compare_snapshot: PERIPHERAL_ADC_SCAN timeout\n");
        selftest_check_record(check_id, false);
        return;
    }

    /* Parse current scan */
    int current[LTC_CH_COUNT];
    for (int i = 0; i < LTC_CH_COUNT; i++) {
        char needle[6];
        snprintf(needle, sizeof(needle), "CH%02d", i);
        const char *p = strstr(buf, needle);
        if (!p) { current[i] = -1; continue; }
        p += 4;
        while (*p == ' ') p++;
        current[i] = ((*p >= '0' && *p <= '9') || *p == '-') ? (int)strtol(p, NULL, 10) : -1;
    }

    int delta_count = 0;
    for (int i = 0; i < LTC_CH_COUNT; i++) {
        if (baseline->mv[i] < 0 || current[i] < 0) continue;  /* error in either scan */

        /* Check ignore list */
        bool skip = false;
        if (ignore_arr) {
            int arr_sz = cJSON_GetArraySize(ignore_arr);
            for (int k = 0; k < arr_sz && !skip; k++) {
                const cJSON *item = cJSON_GetArrayItem(ignore_arr, k);
                if (item && cJSON_IsNumber(item) && (int)item->valuedouble == i)
                    skip = true;
            }
        }
        if (skip) continue;

        int delta = current[i] - baseline->mv[i];
        if (delta < 0) delta = -delta;
        if (delta > noise_mv) {
            printf("[WARN] ltc2498_compare_snapshot: CH%02d delta=%d mV"
                   "  (was %d mV, now %d mV)\n",
                   i, delta, baseline->mv[i], current[i]);
            delta_count++;
        }
    }

    bool pass = (delta_count == 0);
    printf("[%s] ltc2498_compare_snapshot: \"%s\"  %d unexpected deltas  (noise_mv=%d)\n",
           pass ? "PASS" : "FAIL", name, delta_count, noise_mv);
    selftest_check_record(check_id, pass);
}

/* ── sig_inject ────────────────────────────────────────────────────────────── *
 * Drive U8 signal injection mux to a specific channel and level.
 * Used to stimulate DUT inputs for Scenario B tests.
 *
 * Params:
 *   "u8_ch"    : int     U8 channel (0–7)
 *   "level"    : int     SIG level (0 or 1, default 0 = active low)
 *   "hold_ms"  : int     hold time before returning (default 50)
 */
void run_sig_inject(const cJSON *params)
{
    int u8_ch   = param_int(params, "u8_ch", 0);
    int level   = param_int(params, "level", 0);
    int hold_ms = param_int(params, "hold_ms", 50);

    u8_mux_select(u8_ch, level);
    vTaskDelay(pdMS_TO_TICKS(hold_ms));
    /* Caller is responsible for u8_mux_release() via sig_release step */
}

/* ── sig_release ───────────────────────────────────────────────────────────── *
 * Release U8 signal injection mux (float SIG to high-Z).
 */
void run_sig_release(const cJSON *params)
{
    (void)params;
    u8_mux_release();
}

/* ── button_test ───────────────────────────────────────────────────────────── *
 * Composite Scenario B test for a button input:
 *   1. Verify DUT reads resting state (HIGH — internal pull-up)
 *   2. TC drives U8 channel LOW (simulates button press)
 *   3. Verify DUT reads driven state (LOW)
 *   4. TC releases U8
 *   5. Verify DUT reads restored resting state (HIGH)
 *
 * Params:
 *   "pin"      : string  DUT GPIO pin name (e.g. "PA11")
 *   "u8_ch"    : int     U8 channel for this button
 *   "hold_ms"  : int     how long to hold drive before reading (default 50)
 *   "check_id" : string  base check ID (appended with _rest, _drv, _rel)
 */
void run_button_test(const cJSON *params)
{
    const char *pin      = param_str(params, "pin", NULL);
    int u8_ch            = param_int(params, "u8_ch", 0);
    int hold_ms          = param_int(params, "hold_ms", 50);
    const char *check_id = param_str(params, "check_id", "button");

    if (!pin) {
        printf("[FAIL] button_test: missing 'pin' param\n");
        selftest_check_record(check_id, false);
        return;
    }
    if (!dut_uart_is_open()) {
        printf("[FAIL] button_test: UART not open\n");
        selftest_check_record(check_id, false);
        return;
    }

    char cmd[64], resp[128];
    char id_buf[64];

    /* Step 1: Read resting state — expect HIGH (pull-up) */
    snprintf(cmd, sizeof(cmd), "PIN_READ %s", pin);
    bool ok1 = dut_cmd(cmd, resp, sizeof(resp), 500);
    bool rest_ok = ok1 && (strstr(resp, "HIGH") != NULL);
    snprintf(id_buf, sizeof(id_buf), "%s_rest", check_id);
    printf("[%s] button_test: %s resting  %s  (exp HIGH)\n",
           rest_ok ? "PASS" : "FAIL", pin, resp);
    selftest_check_record(id_buf, rest_ok);

    /* Step 2: Drive LOW via U8 */
    u8_mux_select(u8_ch, 0);  /* active low */
    vTaskDelay(pdMS_TO_TICKS(hold_ms));

    /* Step 3: Read driven state — expect LOW */
    bool ok2 = dut_cmd(cmd, resp, sizeof(resp), 500);
    bool drv_ok = ok2 && (strstr(resp, "LOW") != NULL);
    snprintf(id_buf, sizeof(id_buf), "%s_drv", check_id);
    printf("[%s] button_test: %s driven   %s  (exp LOW)\n",
           drv_ok ? "PASS" : "FAIL", pin, resp);
    selftest_check_record(id_buf, drv_ok);

    /* Step 4: Release U8 */
    u8_mux_release();
    vTaskDelay(pdMS_TO_TICKS(hold_ms));

    /* Step 5: Read restored state — expect HIGH */
    bool ok3 = dut_cmd(cmd, resp, sizeof(resp), 500);
    bool rel_ok = ok3 && (strstr(resp, "HIGH") != NULL);
    snprintf(id_buf, sizeof(id_buf), "%s_rel", check_id);
    printf("[%s] button_test: %s released %s  (exp HIGH)\n",
           rel_ok ? "PASS" : "FAIL", pin, resp);
    selftest_check_record(id_buf, rel_ok);
}
