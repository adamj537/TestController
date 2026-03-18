#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include "esp_console.h"
#include "esp_wifi.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "version.h"
#include "cmd_i2c.h"
#include "cmd_adc.h"
#include "cmd_vdac.h"
#include "cmd_selftest.h"
#include "tc_mqtt.h"
#include "recipe_primitives.h"
#include "dut_detect.h"
#include "tc_statemachine.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "tie_pinmap.h"

/* ── TCC carrier device map ───────────────────────────────────────────────── */

#define ADDR_ADC128D818  0x1D   /* 8-ch analog input ADC */
#define ADDR_INA219_0    0x40   /* Current sense #0 */
#define ADDR_INA219_1    0x41   /* Current sense #1 */

/* ── Result tracking ──────────────────────────────────────────────────────── */

static int s_pass;
static int s_total;

static void report(bool ok, const char *fmt, ...)
{
    s_total++;
    if (ok) s_pass++;
    printf("[%s] ", ok ? "PASS" : "FAIL");
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/* ── Structured check collection (for MQTT selftest DDATA) ───────────────── */

#define MAX_CHECKS 32
static tc_mqtt_check_t s_checks[MAX_CHECKS];
static int             s_ncheck;

static void st_reset(void)
{
    s_ncheck = 0;
}

static void st_record(const char *id, bool pass)
{
    if (s_ncheck >= MAX_CHECKS) return;
    s_checks[s_ncheck].id        = id;
    s_checks[s_ncheck].pass      = pass;
    s_checks[s_ncheck].has_value = false;
    s_checks[s_ncheck].value     = 0;
    s_ncheck++;
}

static void st_record_mv(const char *id, bool pass, int mv)
{
    if (s_ncheck >= MAX_CHECKS) return;
    s_checks[s_ncheck].id        = id;
    s_checks[s_ncheck].pass      = pass;
    s_checks[s_ncheck].has_value = true;
    s_checks[s_ncheck].value     = mv;
    s_ncheck++;
}

/* ── ADC128D818 register map (subset) ────────────────────────────────────── */

#define ADC128_REG_CONFIG    0x00  /* bit0=START, bit7=INIT(resets all regs, self-clearing) */
#define ADC128_REG_CONV_RATE 0x07  /* 0=low-power (~728ms/scan), 1=high-rate (~12ms/ch) */
#define ADC128_REG_CH_DIS    0x08  /* bit N = disable channel N */
#define ADC128_REG_ADV_CFG   0x0B  /* bit0=ext-VREF-en, bits[2:1]=mode: 0x02=Mode1(all 8 pins voltage) */
#define ADC128_REG_CH_BASE  0x20  /* CH0=0x20 … CH7=0x27, 2 bytes each   */
                                  /* value = ((b0<<8)|b1)>>4, Vref=2560mV */

/* Divider constants (expressed as scaled integers to avoid float):
 *   CH4: /5V_MON   R_top=14.3K R_bot=10K → ratio 10/24.3 → V_rail = V_adc * 243 / 100
 *   CH5: /3V3_MON  R_top=6K    R_bot=10K → ratio 10/16   → V_rail = V_adc * 8 / 5    */
#define ADC128_CH4_NUM  243     /* numerator   of inverse divider (CH4 = /5V_MON)  */
#define ADC128_CH4_DEN  100     /* denominator of inverse divider */
#define ADC128_CH5_NUM  8       /* numerator   of inverse divider (CH5 = /3V3_MON) */
#define ADC128_CH5_DEN  5       /* denominator of inverse divider */

#define ADC128_VREF_MV  2560
#define ADC128_FULL     4096

/* Pass/fail window ±5 % on reconstructed rail voltage */
#define ADC128_TOL_PCT  5

static bool adc128_read_channel(uint8_t ch, int *rail_mv_out)
{
    uint8_t buf[2];
    if (!i2c_read_reg(ADDR_ADC128D818, ADC128_REG_CH_BASE + ch, buf, 2)) return false;
    int count    = ((buf[0] << 8) | buf[1]) >> 4;
    int adc_mv   = count * ADC128_VREF_MV / ADC128_FULL;
    /* scale back to rail voltage using per-channel divider ratio */
    if (ch == 4) *rail_mv_out = adc_mv * ADC128_CH4_NUM / ADC128_CH4_DEN;
    else         *rail_mv_out = adc_mv * ADC128_CH5_NUM / ADC128_CH5_DEN;
    return true;
}

/* ── Individual test groups ───────────────────────────────────────────────── */

/* Reads ADC128D818 CH7 (internal temperature diode) by temporarily switching
 * to Mode 0 (IN7 → temp diode), then restoring Mode 1 (all voltage inputs).
 *
 * Conversion per SNAS483F Table 16, Equations 2 & 3:
 *   The temperature register is 9-bit two's complement, 0.5°C/LSB,
 *   left-justified in the 16-bit channel register → extract with >> 7.
 *   If DOUT[8]=0 (positive): Temp = DOUT / 2           (Eq 2)
 *   If DOUT[8]=1 (negative): Temp = (DOUT − 512) / 2  (Eq 3)
 *   Accuracy: ±2°C over −25°C to 100°C. */
static void run_temp(void)
{
    if (!i2c_write_reg(ADDR_ADC128D818, ADC128_REG_ADV_CFG,   0x00) ||
        !i2c_write_reg(ADDR_ADC128D818, ADC128_REG_CONV_RATE, 0x01) ||
        !i2c_write_reg(ADDR_ADC128D818, ADC128_REG_CONFIG,    0x01)) {
        report(false, "ADC128: CH7  temp sensor  (mode-0 switch failed)");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(150));

    uint8_t tbuf[2];
    if (!i2c_read_reg(ADDR_ADC128D818, ADC128_REG_CH_BASE + 7, tbuf, 2)) {
        report(false, "ADC128: CH7  temp sensor  read error");
        st_record("adc_temp", false);
    } else {
        /* Extract 9-bit code (left-justified in 16-bit register) */
        int dout   = (int)((uint16_t)((tbuf[0] << 8) | tbuf[1]) >> 7);
        int temp_x2 = (dout & 0x100) ? (dout - 512) : dout;  /* two's complement */

        /* Render with sign-safe absolute-value decomposition */
        bool neg      = temp_x2 < 0;
        int  abs_x2   = neg ? -temp_x2 : temp_x2;
        int  temp_int  = abs_x2 / 2;
        int  temp_frac = (abs_x2 & 1) ? 5 : 0;  /* 0 or 5 tenths */

        /* Sanity: expect lab ambient -10–85°C */
        bool sane = (temp_x2 >= -20 && temp_x2 <= 170);
        report(sane, "ADC128: CH7  temp sensor  %s%d.%d°C  (±2°C, exp -10–85°C)",
               neg ? "-" : "", temp_int, temp_frac);
        st_record("adc_temp", sane);
    }

    /* Restore Mode 1 so voltage channels work after selftest */
    i2c_write_reg(ADDR_ADC128D818, ADC128_REG_ADV_CFG,   0x02);
    i2c_write_reg(ADDR_ADC128D818, ADC128_REG_CONV_RATE, 0x01);
    i2c_write_reg(ADDR_ADC128D818, ADC128_REG_CONFIG,    0x01);
}

static void run_adc128(void)
{
    /* Mode 1: all 8 pins as voltage inputs (default Mode 0 maps IN7 to temp sensor).
     * ADV_CFG and CONV_RATE must be written before START bit.
     * At high rate: ~12ms/channel × 8 channels ≈ 100ms for first full scan. */
    if (!i2c_write_reg(ADDR_ADC128D818, ADC128_REG_ADV_CFG,   0x02) ||
        !i2c_write_reg(ADDR_ADC128D818, ADC128_REG_CONV_RATE, 0x01) ||
        !i2c_write_reg(ADDR_ADC128D818, ADC128_REG_CONFIG,    0x01)) {
        report(false, "ADC128: init failed");
        report(false, "ADC128: CH4  5.0V rail  (skipped)");
        report(false, "ADC128: CH5  3.3V rail  (skipped)");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(150));  /* allow first high-rate conversion cycle */

    /* CH4 — 5.0 V rail (/5V_MON) */
    int rail_mv = 0;
    if (!adc128_read_channel(4, &rail_mv)) {
        report(false, "ADC128: CH4  5.0V rail  read error");
        st_record_mv("adc_rail_5v", false, 0);
    } else {
        int lo = 5000 * (100 - ADC128_TOL_PCT) / 100;
        int hi = 5000 * (100 + ADC128_TOL_PCT) / 100;
        bool ok = rail_mv >= lo && rail_mv <= hi;
        report(ok, "ADC128: CH4  5.0V rail  %4d mV  (exp %d–%d mV)", rail_mv, lo, hi);
        st_record_mv("adc_rail_5v", ok, rail_mv);
    }

    /* CH5 — 3.3 V rail (/3V3_MON) */
    if (!adc128_read_channel(5, &rail_mv)) {
        report(false, "ADC128: CH5  3.3V rail  read error");
        st_record_mv("adc_rail_3v3", false, 0);
    } else {
        int lo = 3300 * (100 - ADC128_TOL_PCT) / 100;
        int hi = 3300 * (100 + ADC128_TOL_PCT) / 100;
        bool ok = rail_mv >= lo && rail_mv <= hi;
        report(ok, "ADC128: CH5  3.3V rail  %4d mV  (exp %d–%d mV)", rail_mv, lo, hi);
        st_record_mv("adc_rail_3v3", ok, rail_mv);
    }

    /* Append temperature read on the same bus */
    run_temp();
}

/* HW-012: I2C swap errata resolved — all devices on same bus (SDA=15, SCL=16). */
void run_i2c(void)
{
    bool bus_ok = i2c_ensure_initialized();
    report(bus_ok, "I2C: bus init  (SDA=GPIO15  SCL=GPIO16  400kHz)");
    st_record("i2c_bus", bus_ok);
    if (!bus_ok) {
        report(false, "I2C: INA219 #0  @ 0x%02X  (skipped — bus not ready)", ADDR_INA219_0);
        st_record("i2c_ina219_0", false);
        report(false, "I2C: INA219 #1  @ 0x%02X  (skipped — bus not ready)", ADDR_INA219_1);
        st_record("i2c_ina219_1", false);
        report(false, "I2C: ADC128D818 @ 0x%02X  (skipped — bus not ready)", ADDR_ADC128D818);
        st_record("i2c_adc128", false);
        return;
    }
    bool ina0_ok = i2c_probe(ADDR_INA219_0);
    report(ina0_ok, "I2C: INA219 #0  @ 0x%02X", ADDR_INA219_0);
    st_record("i2c_ina219_0", ina0_ok);

    bool ina1_ok = i2c_probe(ADDR_INA219_1);
    report(ina1_ok, "I2C: INA219 #1  @ 0x%02X", ADDR_INA219_1);
    st_record("i2c_ina219_1", ina1_ok);

    bool adc128_ok = i2c_probe(ADDR_ADC128D818);
    report(adc128_ok, "I2C: ADC128D818 @ 0x%02X", ADDR_ADC128D818);
    st_record("i2c_adc128", adc128_ok);
    if (adc128_ok) run_adc128();
}

void run_adc(void)
{
    int raw = 0, mv = -1;
    bool ok = adc_selftest_read(0, &raw, &mv);
    if (ok && mv >= 0) {
        report(true,  "ADC:  CH0 (GPIO1) raw=%-5d  %4d mV", raw, mv);
        st_record_mv("adc_gpio1", true, mv);
    } else if (ok) {
        report(true,  "ADC:  CH0 (GPIO1) raw=%-5d  (no calibration)", raw);
        st_record("adc_gpio1", true);
    } else {
        report(false, "ADC:  CH0 (GPIO1) read failed");
        st_record("adc_gpio1", false);
    }
}

void run_wifi(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        report(true,  "WiFi: connected  SSID=%s  RSSI=%d dBm", (char *)ap.ssid, ap.rssi);
        st_record_mv("wifi", true, ap.rssi);
    } else {
        report(false, "WiFi: not connected  (run: wifi connect <ssid> <pass>)");
        st_record("wifi", false);
    }
}

void run_ota(void)
{
    const esp_partition_t *p   = esp_ota_get_running_partition();
    const esp_app_desc_t  *d   = esp_app_get_description();
    esp_ota_img_states_t   state = ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(p, &state);

    const char *state_str = "unknown";
    bool state_ok = false;
    switch (state) {
        case ESP_OTA_IMG_VALID:           state_str = "valid";          state_ok = true; break;
        case ESP_OTA_IMG_UNDEFINED:       state_str = "undefined";      state_ok = true; break;
        case ESP_OTA_IMG_NEW:             state_str = "new";            break;
        case ESP_OTA_IMG_PENDING_VERIFY:  state_str = "pending_verify"; break;
        case ESP_OTA_IMG_INVALID:         state_str = "invalid";        break;
        case ESP_OTA_IMG_ABORTED:         state_str = "aborted";        break;
    }
    report(state_ok, "OTA:  partition=%-8s  state=%-15s  fw=%s",
           p->label, state_str, d->version);
    st_record("ota", state_ok);
}

/* ── VDUT 3-point selfcheck ───────────────────────────────────────────────── *
 * Sweeps VDUT1 and VDUT2 through three duty-cycle setpoints.  INA219 V_BUS
 * is the primary voltage measurement (±0.5% max at room temp, measured at
 * the DUT connector — after the 0.1Ω shunt, which drops <0.1mV at 1mA).
 * ADC128 CH6/CH7 are no longer used here; those channels are reserved for
 * DUT_VIN and ISO_POWER_IN monitoring on the next board spin.
 *
 * Pass criteria:
 *   1. INA219 V_BUS within the per-point sanity window.
 *   2. Monotonic response: V(10%) > V(50%) > V(90%) for both channels.
 *
 * Settling: VDAC_INIT_SETTLE_MS + 3 × VDAC_STEP_SETTLE_MS ≈ 6.5 s total. */

#define INA219_REG_CONFIG   0x00
#define INA219_REG_SHUNT_V  0x01
#define INA219_REG_BUS_V    0x02
#define INA219_REG_POWER    0x03
#define INA219_REG_CURRENT  0x04
#define INA219_REG_CAL      0x05

/* PGA=/1 (±40mV FSR) gives 4× better offset spec than the default PGA=/8 (±320mV):
 *   PGA=/8 offset ±40µV RTI — at 1mA×0.1Ω=100µV shunt, that is ±40% error.
 *   PGA=/1 offset ±10µV RTI — at 100µV shunt, ±10% error. Max range = ±400mA.
 * Config 0x219F: BRNG=1(32V bus), PG[1:0]=00(PGA=/1,±40mV), BADC/SADC=12bit, MODE=111(cont)
 * Cal = trunc(0.04096 / (Current_LSB × R_shunt)) = trunc(0.04096 / (10e-6 × 0.1)) = 40960 */
#define INA219_CFG_PGA1         0x219F
#define INA219_CAL_PGA1         0xA000   /* 40960 */
#define INA219_CURRENT_LSB_UA   10       /* µA per Current register LSB */
#define INA219_POWER_LSB_UW     200      /* µW per Power register LSB (= 20 × I_LSB) */

static const int s_vdut_duty_pct[3] = { 10, 50, 90 };

/* Conservative absolute windows.  Regulator output is non-linear;
 * the monotonic check is the primary functional assertion. */
static const int s_vdut_lo_mv[3]    = {  8000, 5000, 1000 };
static const int s_vdut_hi_mv[3]    = { 12000, 9000, 5000 };

void selftest_ina219_init(void)
{
    i2c_ensure_initialized();
    i2c_write_reg16(ADDR_INA219_0, INA219_REG_CONFIG, INA219_CFG_PGA1);
    i2c_write_reg16(ADDR_INA219_1, INA219_REG_CONFIG, INA219_CFG_PGA1);
    i2c_write_reg16(ADDR_INA219_0, INA219_REG_CAL,    INA219_CAL_PGA1);
    i2c_write_reg16(ADDR_INA219_1, INA219_REG_CAL,    INA219_CAL_PGA1);
}

void run_vdut(void)
{
    /* ── SAFETY: VDUT voltage sweep must NEVER run with DUT connected ──── *
     * The sweep drives 10V+ (10% duty) into a ~4V-rated DUT board.        *
     * Check DUT presence BEFORE enabling any VDUT output.                  *
     *                                                                       *
     * NOTE: dut_detect_sample() uses U8 ch3 signal injection + ADC128 read.*
     * It must run before VDUT is enabled to avoid interference.            */
    {
        int mv = 0;
        bool sampled = dut_detect_sample(&mv);
        printf("VDUT safety: DUT detect = %d mV (%s)\n",
               mv, sampled ? (mv < 1500 ? "PRESENT" : "ABSENT") : "SAMPLE FAILED");
        if (sampled && mv < 1500) {
            report(false, "VDUT: BLOCKED — DUT detected (%d mV). "
                   "Remove DUT before running voltage sweep.", mv);
            st_record("vdut_safety", false);
            return;
        }
    }

    /* ── 1. LEDC at 0 %, enable both regulators ────────────────────────── */
    if (!vdac_set_duty(0, 0) || !vdac_set_duty(1, 0)) {
        report(false, "VDUT: LEDC init failed"); return;
    }
    if (!vdac_set_enable(0, true) || !vdac_set_enable(1, true)) {
        report(false, "VDUT: enable GPIO config failed"); return;
    }

    /* ── 2. Configure INA219s — normal bus throughout, no ADC128 swap ─── */
    i2c_ensure_initialized();
    i2c_write_reg16(ADDR_INA219_0, INA219_REG_CONFIG, INA219_CFG_PGA1);
    i2c_write_reg16(ADDR_INA219_1, INA219_REG_CONFIG, INA219_CFG_PGA1);
    i2c_write_reg16(ADDR_INA219_0, INA219_REG_CAL, INA219_CAL_PGA1);
    i2c_write_reg16(ADDR_INA219_1, INA219_REG_CAL, INA219_CAL_PGA1);
    vTaskDelay(pdMS_TO_TICKS(VDAC_INIT_SETTLE_MS));

    /* ── 3. Three-point sweep ──────────────────────────────────────────── */
    int  bus0[3] = {0}, bus1[3] = {0};
    int  cur0[3] = {0}, cur1[3] = {0};
    int  pwr0[3] = {0}, pwr1[3] = {0};
    bool iok[3][2] = {{false}};

    for (int p = 0; p < 3; p++) {
        vdac_set_duty(0, s_vdut_duty_pct[p]);
        vdac_set_duty(1, s_vdut_duty_pct[p]);
        vTaskDelay(pdMS_TO_TICKS(VDAC_STEP_SETTLE_MS));

        uint8_t buf[2];
        if (i2c_read_reg(ADDR_INA219_0, INA219_REG_BUS_V, buf, 2)) {
            bus0[p] = (((int16_t)((buf[0] << 8) | buf[1])) >> 3) * 4;
            iok[p][0] = true;
        }
        if (i2c_read_reg(ADDR_INA219_0, INA219_REG_CURRENT, buf, 2))
            cur0[p] = (int16_t)((buf[0] << 8) | buf[1]);
        if (i2c_read_reg(ADDR_INA219_0, INA219_REG_POWER, buf, 2))
            pwr0[p] = (buf[0] << 8) | buf[1];

        if (i2c_read_reg(ADDR_INA219_1, INA219_REG_BUS_V, buf, 2)) {
            bus1[p] = (((int16_t)((buf[0] << 8) | buf[1])) >> 3) * 4;
            iok[p][1] = true;
        }
        if (i2c_read_reg(ADDR_INA219_1, INA219_REG_CURRENT, buf, 2))
            cur1[p] = (int16_t)((buf[0] << 8) | buf[1]);
        if (i2c_read_reg(ADDR_INA219_1, INA219_REG_POWER, buf, 2))
            pwr1[p] = (buf[0] << 8) | buf[1];
    }

    /* ── 4. Cleanup ────────────────────────────────────────────────────── */
    vdac_set_duty(0, 0); vdac_set_duty(1, 0);
    vdac_set_enable(0, false); vdac_set_enable(1, false);

    /* ── 5. Report — INA219 V_BUS is the primary voltage measurement ───── */
    static const char *vdut1_ids[3] = { "vdut1_10pct", "vdut1_50pct", "vdut1_90pct" };
    static const char *vdut2_ids[3] = { "vdut2_10pct", "vdut2_50pct", "vdut2_90pct" };

    for (int p = 0; p < 3; p++) {
        bool v1_ok = iok[p][0] && bus0[p] >= s_vdut_lo_mv[p] && bus0[p] <= s_vdut_hi_mv[p];
        bool v2_ok = iok[p][1] && bus1[p] >= s_vdut_lo_mv[p] && bus1[p] <= s_vdut_hi_mv[p];

        report(v1_ok,
               "VDUT1: duty %2d%%  INA0_Vbus %5d mV  INA0_cur %+5d µA  INA0_pwr %5d µW  (exp %d-%d)",
               s_vdut_duty_pct[p], iok[p][0] ? bus0[p] : -1,
               cur0[p] * INA219_CURRENT_LSB_UA,
               pwr0[p] * INA219_POWER_LSB_UW,
               s_vdut_lo_mv[p], s_vdut_hi_mv[p]);
        st_record_mv(vdut1_ids[p], v1_ok, iok[p][0] ? bus0[p] : 0);

        report(v2_ok,
               "VDUT2: duty %2d%%  INA1_Vbus %5d mV  INA1_cur %+5d µA  INA1_pwr %5d µW  (exp %d-%d)",
               s_vdut_duty_pct[p], iok[p][1] ? bus1[p] : -1,
               cur1[p] * INA219_CURRENT_LSB_UA,
               pwr1[p] * INA219_POWER_LSB_UW,
               s_vdut_lo_mv[p], s_vdut_hi_mv[p]);
        st_record_mv(vdut2_ids[p], v2_ok, iok[p][1] ? bus1[p] : 0);
    }

    /* Monotonic check: duty↑ → voltage↓ */
    bool mono1 = iok[0][0] && iok[1][0] && iok[2][0] && (bus0[0] > bus0[1]) && (bus0[1] > bus0[2]);
    bool mono2 = iok[0][1] && iok[1][1] && iok[2][1] && (bus1[0] > bus1[1]) && (bus1[1] > bus1[2]);
    report(mono1, "VDUT1: monotonic  %d > %d > %d mV  (duty 10%%→50%%→90%%)", bus0[0], bus0[1], bus0[2]);
    st_record("vdut1_mono", mono1);
    report(mono2, "VDUT2: monotonic  %d > %d > %d mV  (duty 10%%→50%%→90%%)", bus1[0], bus1[1], bus1[2]);
    st_record("vdut2_mono", mono2);
}

/* ── VDUT characterization sweep ──────────────────────────────────────────── *
 * Sweeps VDUT1/VDUT2 0–100% in 5% steps.  For each point reads:
 *   INA219 #0/#1    — Vbus (mV), Vshunt (µV), Current (µA), Power (µW)
 *
 * INA219 V_BUS is the primary voltage measurement (±0.5% max at room temp).
 * ADC128 CH6/CH7 are reserved for DUT_VIN / ISO_POWER_IN on the next board spin.
 * Not included in selftest all — takes ~33 s (21 steps × 1500ms settle). */
static void run_char(void)
{
    /* 1. LEDC at 0%, enable both regulators */
    if (!vdac_set_duty(0, 0) || !vdac_set_duty(1, 0)) {
        printf("VDUT: LEDC init failed\n"); return;
    }
    if (!vdac_set_enable(0, true) || !vdac_set_enable(1, true)) {
        printf("VDUT: enable GPIO failed\n"); return;
    }

    /* 2. Configure INA219s — normal bus throughout, no ADC128 swap needed */
    i2c_ensure_initialized();
    i2c_write_reg16(ADDR_INA219_0, INA219_REG_CONFIG, INA219_CFG_PGA1);
    i2c_write_reg16(ADDR_INA219_1, INA219_REG_CONFIG, INA219_CFG_PGA1);
    i2c_write_reg16(ADDR_INA219_0, INA219_REG_CAL, INA219_CAL_PGA1);
    i2c_write_reg16(ADDR_INA219_1, INA219_REG_CAL, INA219_CAL_PGA1);

    /* 3. Initial settle */
    printf("Settling %d ms...\n", VDAC_INIT_SETTLE_MS);
    vTaskDelay(pdMS_TO_TICKS(VDAC_INIT_SETTLE_MS));

    /* 4. Header */
    printf("VDAC Char  (PGA=/1  R=0.1Ω  I_LSB=%dµA  P_LSB=%dµW  settle=%dms/step)\n",
           INA219_CURRENT_LSB_UA, INA219_POWER_LSB_UW, VDAC_STEP_SETTLE_MS);
    printf("duty  VBus0  Vsht0   I0      P0     VBus1  Vsht1   I1      P1\n");
    printf("  %%    mV     µV      µA     µW      mV     µV      µA     µW\n");
    printf("----  -----  ------  ------  ------  -----  ------  ------  ------\n");

    /* 5. Sweep 0–100% in 5% steps */
    for (int pct = 0; pct <= 100; pct += 5) {
        vdac_set_duty(0, pct);
        vdac_set_duty(1, pct);
        vTaskDelay(pdMS_TO_TICKS(VDAC_STEP_SETTLE_MS));

        uint8_t buf[2];
        int bus0 = 0, sht0 = 0, cur0 = 0, pwr0 = 0;
        int bus1 = 0, sht1 = 0, cur1 = 0, pwr1 = 0;

        if (i2c_read_reg(ADDR_INA219_0, INA219_REG_SHUNT_V, buf, 2))
            sht0 = (int16_t)((buf[0] << 8) | buf[1]);
        if (i2c_read_reg(ADDR_INA219_0, INA219_REG_BUS_V, buf, 2))
            bus0 = (((int16_t)((buf[0] << 8) | buf[1])) >> 3) * 4;
        if (i2c_read_reg(ADDR_INA219_0, INA219_REG_CURRENT, buf, 2))
            cur0 = (int16_t)((buf[0] << 8) | buf[1]);
        if (i2c_read_reg(ADDR_INA219_0, INA219_REG_POWER, buf, 2))
            pwr0 = (buf[0] << 8) | buf[1];

        if (i2c_read_reg(ADDR_INA219_1, INA219_REG_SHUNT_V, buf, 2))
            sht1 = (int16_t)((buf[0] << 8) | buf[1]);
        if (i2c_read_reg(ADDR_INA219_1, INA219_REG_BUS_V, buf, 2))
            bus1 = (((int16_t)((buf[0] << 8) | buf[1])) >> 3) * 4;
        if (i2c_read_reg(ADDR_INA219_1, INA219_REG_CURRENT, buf, 2))
            cur1 = (int16_t)((buf[0] << 8) | buf[1]);
        if (i2c_read_reg(ADDR_INA219_1, INA219_REG_POWER, buf, 2))
            pwr1 = (buf[0] << 8) | buf[1];

        printf(" %3d  %5d  %+6d  %+6d  %6d  %5d  %+6d  %+6d  %6d\n",
               pct,
               bus0, sht0 * 10, cur0 * INA219_CURRENT_LSB_UA, pwr0 * INA219_POWER_LSB_UW,
               bus1, sht1 * 10, cur1 * INA219_CURRENT_LSB_UA, pwr1 * INA219_POWER_LSB_UW);
    }

    /* 6. Cleanup */
    vdac_set_duty(0, 0); vdac_set_duty(1, 0);
    vdac_set_enable(0, false); vdac_set_enable(1, false);
    printf("Done.\n");
}

/* ── TIE analog mux scan ──────────────────────────────────────────────────── *
 * Four HEF4067BTT 16-channel muxes route DUT signals to ADC128D818 CH0–CH3.  *
 * /EN pins are hardwired to GND (always enabled).                             *
 * Address lines A0–A3 select which of the 16 DUT inputs is routed to the     *
 * mux's common output (= the ADC128 input).                                   *
 *                                                                             *
 *  MUX0 (U3)  A0=GPIO3  A1=GPIO4  A2=GPIO5  A3=GPIO6  → ADC128 CH0          *
 *  MUX1 (U11) A0=GPIO7  A1=GPIO8  A2=GPIO9  A3=GPIO10 → ADC128 CH1          *
 *  MUX2 (U12) A0=GPIO11 A1=GPIO12 A2=GPIO13 A3=GPIO14 → ADC128 CH2          *
 *  MUX3 (U1)  A0=GPIO39 A1=GPIO40 A2=GPIO41 A3=GPIO42 → ADC128 CH3          */

#define TIE_MUX_COUNT    4
#define TIE_MUX_CHANNELS 16

static const int s_mux_gpio[TIE_MUX_COUNT][4] = {
    { BSP_TIE_MUX0_A0, BSP_TIE_MUX0_A1, BSP_TIE_MUX0_A2, BSP_TIE_MUX0_A3 },  /* MUX0 (U3)  → ADC128 CH0 */
    { BSP_TIE_MUX1_A0, BSP_TIE_MUX1_A1, BSP_TIE_MUX1_A2, BSP_TIE_MUX1_A3 },  /* MUX1 (U11) → ADC128 CH1 */
    { BSP_TIE_MUX2_A0, BSP_TIE_MUX2_A1, BSP_TIE_MUX2_A2, BSP_TIE_MUX2_A3 },  /* MUX2 (U12) → ADC128 CH2 */
    { BSP_TIE_MUX3_A0, BSP_TIE_MUX3_A1, BSP_TIE_MUX3_A2, BSP_TIE_MUX3_A3 },  /* MUX3 (U1)  → ADC128 CH3 */
};

static const uint8_t s_mux_adc128_ch[TIE_MUX_COUNT] = { 0, 1, 2, 3 };

static void mux_select(int mux_idx, int ch)
{
    for (int bit = 0; bit < 4; bit++) {
        gpio_num_t pin = (gpio_num_t)s_mux_gpio[mux_idx][bit];
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_set_level(pin, (ch >> bit) & 1);
    }
}

static bool adc128_read_raw_mv(uint8_t ch, int *mv_out)
{
    uint8_t buf[2];
    if (!i2c_read_reg(ADDR_ADC128D818, ADC128_REG_CH_BASE + ch, buf, 2)) return false;
    int count = ((buf[0] << 8) | buf[1]) >> 4;
    *mv_out = count * ADC128_VREF_MV / ADC128_FULL;
    return true;
}

void run_mux_scan(void)
{
    if (!i2c_write_reg(ADDR_ADC128D818, ADC128_REG_ADV_CFG,   0x02) ||
        !i2c_write_reg(ADDR_ADC128D818, ADC128_REG_CONV_RATE, 0x01) ||
        !i2c_write_reg(ADDR_ADC128D818, ADC128_REG_CONFIG,    0x01)) {
        report(false, "MUX: ADC128 init failed");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(150));

    printf("MUX  CH  A3 A2 A1 A0   mV\n");
    printf("---  --  -- -- -- --  ----\n");

    int err_count = 0;
    for (int m = 0; m < TIE_MUX_COUNT; m++) {
        for (int ch = 0; ch < TIE_MUX_CHANNELS; ch++) {
            mux_select(m, ch);
            vTaskDelay(pdMS_TO_TICKS(20));

            int mv = 0;
            bool ok = adc128_read_raw_mv(s_mux_adc128_ch[m], &mv);
            if (!ok) err_count++;
            printf("  %d  %2d   %d  %d  %d  %d  %s%4d\n",
                   m, ch,
                   (ch >> 3) & 1, (ch >> 2) & 1, (ch >> 1) & 1, ch & 1,
                   ok ? "" : "ERR", ok ? mv : 0);
        }
    }

    /* Release all address GPIOs */
    for (int m = 0; m < TIE_MUX_COUNT; m++)
        for (int bit = 0; bit < 4; bit++)
            gpio_set_direction((gpio_num_t)s_mux_gpio[m][bit], GPIO_MODE_INPUT);

    report(err_count == 0, "MUX: scan complete  %d read errors", err_count);
    st_record("mux_scan", err_count == 0);
}

/* ── DUT heartbeat check ──────────────────────────────────────────────────── *
 * DUT PA9 (SS_ENA_A) is toggled at 1 Hz by the DUT firmware as a liveness    *
 * indicator.  It routes: DUT PA9 → TIE MUX U12 (MUX2) Ch2 → TCC ADC128 CH2. *
 * Select U12 Ch2 (A1=1, A0=A2=A3=0), sample ADC128 CH2 over 2.5 s, and      *
 * verify the signal both rises above and falls below a mid-rail threshold.    */

#define HEARTBEAT_MUX_IDX   2   /* MUX2 = U12 */
#define HEARTBEAT_MUX_CH    2   /* U12 channel 2 = SS_ENA_A */
#define HEARTBEAT_ADC128_CH 2   /* ADC128D818 CH2 = U12 output */
#define HEARTBEAT_THRESH_MV 1500
#define HEARTBEAT_SAMPLES   25  /* 25 × 100 ms = 2.5 s */

void run_dut_heartbeat(void)
{
    if (!i2c_write_reg(ADDR_ADC128D818, ADC128_REG_ADV_CFG,   0x02) ||
        !i2c_write_reg(ADDR_ADC128D818, ADC128_REG_CONV_RATE, 0x01) ||
        !i2c_write_reg(ADDR_ADC128D818, ADC128_REG_CONFIG,    0x01)) {
        report(false, "DUT heartbeat: ADC128 init failed");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(150));

    /* Select U12 Ch2 */
    mux_select(HEARTBEAT_MUX_IDX, HEARTBEAT_MUX_CH);
    vTaskDelay(pdMS_TO_TICKS(20));

    int min_mv = 9999, max_mv = 0;
    for (int i = 0; i < HEARTBEAT_SAMPLES; i++) {
        int mv = 0;
        if (adc128_read_raw_mv(HEARTBEAT_ADC128_CH, &mv)) {
            if (mv < min_mv) min_mv = mv;
            if (mv > max_mv) max_mv = mv;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* Release address GPIOs */
    for (int bit = 0; bit < 4; bit++)
        gpio_set_direction((gpio_num_t)s_mux_gpio[HEARTBEAT_MUX_IDX][bit], GPIO_MODE_INPUT);

    bool saw_low  = min_mv < HEARTBEAT_THRESH_MV;
    bool saw_high = max_mv > HEARTBEAT_THRESH_MV;
    bool hb_ok    = saw_low && saw_high;
    report(hb_ok,
           "DUT heartbeat: PA9→U12Ch2→ADC128CH2  min=%d mV  max=%d mV  (exp toggle <%d/>%d mV)",
           min_mv, max_mv, HEARTBEAT_THRESH_MV, HEARTBEAT_THRESH_MV);
    st_record_mv("dut_heartbeat", hb_ok, max_mv);
}

/* ── DUT UART command/response transport ──────────────────────────────────── *
 * UART1 on GPIO43 (TX→DUT) / GPIO44 (RX←DUT), 115200 8N1, no flow control.  *
 * Driver is opened once per recipe run (in run_dut_enter_test) and closed     *
 * in run_dut_exit_test.  Individual steps check s_uart_open before use.       */

#define DUT_UART_PORT    UART_NUM_1
#define DUT_UART_TX_GPIO 43
#define DUT_UART_RX_GPIO 44
#define DUT_UART_BUF_SZ  512
#define DUT_UART_BAUD    115200

static bool s_uart_open = false;

static bool dut_uart_open(void)
{
    if (s_uart_open) return true;
    uart_config_t cfg = {
        .baud_rate = DUT_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    if (uart_driver_install(DUT_UART_PORT, DUT_UART_BUF_SZ, DUT_UART_BUF_SZ,
                            0, NULL, 0) != ESP_OK) return false;
    if (uart_param_config(DUT_UART_PORT, &cfg) != ESP_OK ||
        uart_set_pin(DUT_UART_PORT, DUT_UART_TX_GPIO, DUT_UART_RX_GPIO,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        uart_driver_delete(DUT_UART_PORT);
        return false;
    }
    s_uart_open = true;
    return true;
}

static void dut_uart_close(void)
{
    if (!s_uart_open) return;
    uart_driver_delete(DUT_UART_PORT);
    s_uart_open = false;
}

/* Send cmd\r\n, read one response line into buf (stripped of \r\n).
 * Returns true if the response starts with "OK". */
static bool dut_cmd(const char *cmd, char *buf, size_t buf_len, int timeout_ms)
{
    if (!s_uart_open || !buf || buf_len == 0) return false;
    buf[0] = '\0';

    uart_flush_input(DUT_UART_PORT);
    uart_write_bytes(DUT_UART_PORT, cmd, strlen(cmd));
    uart_write_bytes(DUT_UART_PORT, "\r\n", 2);
    uart_wait_tx_done(DUT_UART_PORT, pdMS_TO_TICKS(200));

    size_t  pos      = 0;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (esp_timer_get_time() < deadline && pos < buf_len - 1) {
        uint8_t ch = 0;
        if (uart_read_bytes(DUT_UART_PORT, &ch, 1, pdMS_TO_TICKS(10)) == 1) {
            if (ch == '\n') break;
            if (ch != '\r') buf[pos++] = (char)ch;
        }
    }
    buf[pos] = '\0';
    return (strncmp(buf, "OK", 2) == 0);
}

/* ── DUT UART recipe steps ────────────────────────────────────────────────── */

void run_dut_enter_test(void)
{
    char resp[128] = {0};
    if (!dut_uart_open()) {
        report(false, "DUT: UART driver init failed");
        st_record("dut_enter_test", false);
        return;
    }
    /* Allow DUT time to reach its command-ready state after power-on */
    vTaskDelay(pdMS_TO_TICKS(500));
    bool ok = dut_cmd("ENTER_TEST", resp, sizeof(resp), 1000);
    report(ok, "DUT: ENTER_TEST  %s", resp);
    st_record("dut_enter_test", ok);
    if (!ok) dut_uart_close();   /* leave closed so subsequent steps skip cleanly */
}

void run_dut_version(void)
{
    char resp[128] = {0};
    if (!s_uart_open) {
        report(false, "DUT: VERSION  (UART not open — ENTER_TEST failed?)");
        st_record("dut_version", false);
        return;
    }
    bool ok = dut_cmd("VERSION", resp, sizeof(resp), 500);
    report(ok, "DUT: VERSION  %s", resp);
    st_record("dut_version", ok);
}

void run_dut_hw_rev(void)
{
    char resp[128] = {0};
    if (!s_uart_open) {
        report(false, "DUT: HW_REV  (UART not open)");
        st_record("dut_hw_rev", false);
        return;
    }
    bool ok = dut_cmd("HW_REV", resp, sizeof(resp), 500);
    report(ok, "DUT: HW_REV  %s", resp);
    st_record("dut_hw_rev", ok);
}

/* UC_ADC_READ VREF — expect 2.5 V ± 4 % (2400–2600 mV) */
void run_dut_vref(void)
{
    char resp[128] = {0};
    if (!s_uart_open) {
        report(false, "DUT: UC_ADC VREF  (UART not open)");
        st_record("dut_uc_adc_vref", false);
        return;
    }
    bool ok = dut_cmd("UC_ADC_READ VREF", resp, sizeof(resp), 500);
    int mv = 0;
    if (ok) sscanf(resp, "OK UC_ADC_READ VREF %d", &mv);
    bool in_range = ok && mv >= 2400 && mv <= 2600;
    report(in_range, "DUT: UC_ADC VREF  %d mV  (exp 2400–2600 mV)", mv);
    st_record_mv("dut_uc_adc_vref", in_range, mv);
}

/* UC_ADC_READ 3V_RAIL — expect 3.0 V ± 5 % (2850–3150 mV) */
void run_dut_3v_rail(void)
{
    char resp[128] = {0};
    if (!s_uart_open) {
        report(false, "DUT: UC_ADC 3V_RAIL  (UART not open)");
        st_record("dut_uc_adc_3v_rail", false);
        return;
    }
    bool ok = dut_cmd("UC_ADC_READ 3V_RAIL", resp, sizeof(resp), 500);
    int mv = 0;
    if (ok) sscanf(resp, "OK UC_ADC_READ 3V_RAIL %d", &mv);
    bool in_range = ok && mv >= 2850 && mv <= 3150;
    report(in_range, "DUT: UC_ADC 3V_RAIL  %d mV  (exp 2850–3150 mV)", mv);
    st_record_mv("dut_uc_adc_3v_rail", in_range, mv);
}

/* FLASH_TEST — expect "OK FLASH_TEST PASS ..." */
void run_dut_flash_test(void)
{
    char resp[128] = {0};
    if (!s_uart_open) {
        report(false, "DUT: FLASH_TEST  (UART not open)");
        st_record("dut_flash_test", false);
        return;
    }
    bool ok   = dut_cmd("FLASH_TEST", resp, sizeof(resp), 2000);
    bool pass = ok && strstr(resp, "PASS") != NULL;
    report(pass, "DUT: FLASH_TEST  %s", resp);
    st_record("dut_flash_test", pass);
}

/* RTC_READ — expect battery voltage 1550–3600 mV */
void run_dut_rtc_read(void)
{
    char resp[128] = {0};
    if (!s_uart_open) {
        report(false, "DUT: RTC_READ  (UART not open)");
        st_record("dut_rtc_read", false);
        return;
    }
    bool ok = dut_cmd("RTC_READ", resp, sizeof(resp), 500);
    int mv = 0;
    if (ok) sscanf(resp, "OK RTC_READ %d", &mv);
    bool in_range = ok && mv >= 1550 && mv <= 3600;
    report(in_range, "DUT: RTC_READ  %d mV  (exp 1550–3600 mV)", mv);
    st_record_mv("dut_rtc_read", in_range, mv);
}

void run_dut_exit_test(void)
{
    if (!s_uart_open) return;   /* ENTER_TEST failed earlier — skip silently */
    char resp[128] = {0};
    bool ok = dut_cmd("EXIT_TEST", resp, sizeof(resp), 500);
    report(ok, "DUT: EXIT_TEST  %s", resp);
    st_record("dut_exit_test", ok);
    dut_uart_close();
}

/* ── Recipe table and runner ──────────────────────────────────────────────── *
 * Each step has: id (used in MQTT + logs), function pointer, enabled flag.    *
 * Disabled steps print [SKIP] and are excluded from pass/fail counts and      *
 * from the MQTT selftest payload.                                             *
 *                                                                             *
 * Enable/disable steps here as hardware becomes available.                    *
 * DUT pogo-dependent steps are disabled until all pogos are loaded.           */

typedef struct {
    const char *id;
    void        (*fn)(void);
    bool         enabled;
} recipe_step_t;

static recipe_step_t s_fixture_recipe[] = {
    /* ── TCC/TIE carrier checks — no DUT pogos required ──────── */
    { "i2c",                 run_i2c,            true  },
    { "adc",                 run_adc,            true  },
    { "wifi",                run_wifi,           true  },
    { "ota",                 run_ota,            true  },
    { "vdut",                run_vdut,           false },  /* DISABLED — carrier-only sweep, 10V+ damages DUT */
    { "mux_scan",            run_mux_scan,       true  },
    /* ── DUT pogo-dependent — enable as pogos are loaded ─────── */
    { "dut_heartbeat",       run_dut_heartbeat,  false },  /* DUT PA9 pogo */
    { "dut_enter_test",      run_dut_enter_test, false },  /* UART TX/RX pogos */
    { "dut_version",         run_dut_version,    false },
    { "dut_hw_rev",          run_dut_hw_rev,     false },
    { "dut_uc_adc_vref",     run_dut_vref,       false },
    { "dut_uc_adc_3v_rail",  run_dut_3v_rail,    false },
    { "dut_flash_test",      run_dut_flash_test, false },
    { "dut_rtc_read",        run_dut_rtc_read,   false },
    { "dut_exit_test",       run_dut_exit_test,  false },
};

#define RECIPE_LEN  (sizeof(s_fixture_recipe) / sizeof(s_fixture_recipe[0]))

static void run_fixture_recipe(void)
{
    s_uart_open = false;   /* ensure clean UART state at recipe start */

    /* Count enabled steps for progress reporting */
    int enabled_total = 0;
    for (size_t i = 0; i < RECIPE_LEN; i++)
        if (s_fixture_recipe[i].enabled) enabled_total++;

    int step_num = 0;
    for (size_t i = 0; i < RECIPE_LEN; i++) {
        recipe_step_t *step = &s_fixture_recipe[i];
        if (!step->enabled) {
            printf("[SKIP] %s\n", step->id);
            continue;
        }
        /* Resolve primitive via dispatch table (Phase 1 recipe engine) */
        primitive_fn_t fn = recipe_primitives_lookup(step->id);
        if (!fn) {
            printf("[SKIP] %s (primitive not found)\n", step->id);
            continue;
        }
        step_num++;
        tc_mqtt_publish_test_progress(true, step_num, enabled_total, step->id);
        fn();
    }
    /* Safety: ensure UART is closed even if dut_exit_test was skipped or failed */
    if (s_uart_open) dut_uart_close();
}

/* ── Command dispatcher ───────────────────────────────────────────────────── */

static int do_selftest(int argc, char **argv)
{
    s_pass = 0; s_total = 0;
    st_reset();

    bool run_all = (argc < 2 || strcmp(argv[1], "all") == 0);

    if (run_all) {
        printf("=== G3-TC Fixture Recipe ===\n");
        run_fixture_recipe();
    } else if (strcmp(argv[1], "i2c")       == 0) { run_i2c();          }
    else if  (strcmp(argv[1], "adc")       == 0) { run_adc();          }
    else if  (strcmp(argv[1], "wifi")      == 0) { run_wifi();         }
    else if  (strcmp(argv[1], "ota")       == 0) { run_ota();          }
    else if  (strcmp(argv[1], "temp")      == 0) { run_temp();         }
    else if  (strcmp(argv[1], "vdut")      == 0) { run_vdut();         }
    else if  (strcmp(argv[1], "mux")       == 0) { run_mux_scan();     }
    else if  (strcmp(argv[1], "heartbeat") == 0) { run_dut_heartbeat();}
    else if  (strcmp(argv[1], "char")      == 0) { run_char(); return 0; }
    else {
        printf("Unknown test '%s'\n", argv[1]);
        printf("Usage: selftest <i2c|adc|wifi|ota|temp|vdut|mux|heartbeat|char|all>\n");
        return 1;
    }

    printf("\nResults: %d/%d passed\n", s_pass, s_total);

    if (s_ncheck > 0) {
        tc_mqtt_publish_selftest("fixture", s_checks, s_ncheck);
    }

    return (s_pass == s_total) ? 0 : 1;
}

/* ── mac command — unique hardware identity ───────────────────────────────── */

static int do_mac(int argc, char **argv)
{
    (void)argc; (void)argv;

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    printf("MAC   : %02x:%02x:%02x:%02x:%02x:%02x  (WiFi STA)\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    const char *model = (chip.model == CHIP_ESP32S3) ? "ESP32-S3" :
                        (chip.model == CHIP_ESP32S2) ? "ESP32-S2" :
                        (chip.model == CHIP_ESP32)   ? "ESP32"    : "unknown";
    printf("Chip  : %s  rev %d  cores=%d\n", model, chip.revision, chip.cores);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    printf("Flash : %lu MB\n", (unsigned long)(flash_size / (1024 * 1024)));

    printf("FW    : %s\n", FW_VERSION_FULL);

    /* WiFi IP if connected */
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_ip_info_t ip;
            if (esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
                printf("IP    : " IPSTR "\n", IP2STR(&ip.ip));
            }
        }
        printf("SSID  : %s  RSSI=%d dBm\n", (char *)ap.ssid, ap.rssi);
    } else {
        printf("IP    : (not connected)\n");
    }

    return 0;
}

/* ── Async selftest task (used by DCMD and state machine) ─────────────────── */

typedef struct {
    const char *mode;   /* string literal — always valid */
    bool        is_sm;  /* true when spawned by the state machine */
} selftest_task_arg_t;

static void selftest_task(void *pvarg)
{
    selftest_task_arg_t *targ = (selftest_task_arg_t *)pvarg;
    const char *mode = targ->mode;
    bool is_sm = targ->is_sm;
    free(targ);

    s_pass = 0; s_total = 0;
    st_reset();

    printf("[selftest task] mode=%s  sm=%d\n", mode, (int)is_sm);

    if (strcmp(mode, "quick") == 0) {
        /* Quick precheck: I2C bus + INA219 probes + rail voltages + WiFi.
         * Skips VDUT sweep (~6s) and mux scan — fast enough for SM precheck. */
        tc_mqtt_publish_test_progress(true, 1, 2, "i2c_scan");
        run_i2c();
        tc_mqtt_publish_test_progress(true, 2, 2, "wifi");
        run_wifi();
    } else {
        /* fixture: execute recipe (same table as interactive 'selftest all') */
        run_fixture_recipe();
    }

    bool passed = (s_pass == s_total && s_total > 0);
    printf("[selftest task] %d/%d passed\n", s_pass, s_total);

    if (s_ncheck > 0) {
        tc_mqtt_publish_selftest(mode, s_checks, s_ncheck);
    }

    /* Notify state machine if this run was SM-owned.
     * tc_sm_selftest_done() may vTaskDelay internally before returning. */
    if (is_sm) {
        tc_sm_selftest_done(passed, 0 /* duration tracked by SM */);
    }

    vTaskDelete(NULL);
}

void selftest_run_dcmd(const char *mode)
{
    selftest_task_arg_t *arg = malloc(sizeof(*arg));
    if (!arg) return;
    arg->mode  = (strcmp(mode, "quick") == 0) ? "quick" : "fixture";
    arg->is_sm = false;
    xTaskCreate(selftest_task, "selftest_dcmd", 8192, arg, 5, NULL);
}

void selftest_run_sm(const char *mode)
{
    selftest_task_arg_t *arg = malloc(sizeof(*arg));
    if (!arg) return;
    arg->mode  = (strcmp(mode, "quick") == 0) ? "quick" : "fixture";
    arg->is_sm = true;
    xTaskCreate(selftest_task, "selftest_sm", 12288, arg, 5, NULL);
}

/* ── Diagnostic task — single named test primitive ────────────────────────── */

/* Heap-allocated test ID string, freed by diagnostic_task. */
static void diagnostic_task(void *arg)
{
    char *test = (char *)arg;

    s_pass = 0; s_total = 0;
    st_reset();

    printf("[diagnostic] test=%s\n", test);

    if (strcmp(test, "i2c") == 0) {
        run_i2c();
    } else if (strcmp(test, "adc") == 0 || strcmp(test, "adc128") == 0) {
        run_adc128();
    } else if (strcmp(test, "wifi") == 0) {
        run_wifi();
    } else if (strcmp(test, "ota") == 0) {
        run_ota();
    } else if (strcmp(test, "temp") == 0) {
        run_temp();
    } else if (strcmp(test, "vdut") == 0) {
        run_vdut();
    } else if (strcmp(test, "mux") == 0) {
        run_mux_scan();
    } else if (strcmp(test, "heartbeat") == 0) {
        run_dut_heartbeat();
    } else {
        printf("[diagnostic] unknown test '%s'\n", test);
        printf("[diagnostic] valid: i2c adc wifi ota temp vdut mux heartbeat\n");
        free(test);
        vTaskDelete(NULL);
        return;
    }

    printf("[diagnostic] %d/%d passed\n", s_pass, s_total);

    if (s_ncheck > 0) {
        tc_mqtt_publish_selftest(test, s_checks, s_ncheck);
    }

    free(test);
    vTaskDelete(NULL);
}

/* Returns the check ID of the first failing check in the most recent run,
 * or NULL if all checks passed.  String is a literal — always valid. */
const char *selftest_first_failed_check(void)
{
    for (int i = 0; i < s_ncheck; i++) {
        if (!s_checks[i].pass) return s_checks[i].id;
    }
    return NULL;
}

/* ── Check buffer accessors (used by recipe_engine) ──────────────────────── */

int selftest_get_ncheck(void) { return s_ncheck; }
int selftest_get_pass(void)   { return s_pass; }
int selftest_get_total(void)  { return s_total; }
const tc_mqtt_check_t *selftest_get_checks(void) { return s_checks; }

void selftest_reset_checks(void)
{
    s_pass = 0;
    s_total = 0;
    st_reset();
}

void selftest_run_diagnostic(const char *test)
{
    /* Duplicate to heap — task arg must outlive the MQTT event handler */
    char *test_copy = strdup(test);
    if (!test_copy) {
        printf("[diagnostic] alloc failed\n");
        return;
    }
    xTaskCreate(diagnostic_task, "selftest_diag", 8192, test_copy, 5, NULL);
}

void register_selftest_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "selftest",
        .help    = "Bringup selftest: selftest <i2c|adc|wifi|ota|temp|vdut|mux|heartbeat|char|all>",
        .hint    = NULL,
        .func    = &do_selftest,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    const esp_console_cmd_t mac_cmd = {
        .command = "mac",
        .help    = "Print hardware MAC address, chip info, and firmware version",
        .hint    = NULL,
        .func    = &do_mac,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&mac_cmd));
}
