/* tc_cal.h — Calibration storage and application for all measurement channels.
 *
 * All calibration is linear: corrected = gain × raw + offset
 *
 * Channels covered:
 *   VDUT PWM DAC  — slope_mv_per_pct + intercept_mv (inverting regulator model)
 *   INA219 ch0    — Vbus gain/offset_mv, current gain/offset_ma
 *   INA219 ch1    — Vbus gain/offset_mv, current gain/offset_ma
 *   ADC128D818    — gain/offset_mv per channel, ch0–ch7
 *
 * Calibration data is stored in STORAGE_DOMAIN_CALIBRATION, key "cal", as a
 * JSON blob.  Load once at startup; save after updating.
 *
 * Default gain=1.0, offset=0 for all channels → passthrough when uncalibrated.
 * VDUT slope=0 is the uncalibrated sentinel; tc_cal_vdut_duty_for_mv() returns
 * -1 so callers can refuse to enable VDUT before calibration.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── VDUT channel indices ────────────────────────────────────────────────── */

#define TC_CAL_INA_CH0   0
#define TC_CAL_INA_CH1   1
#define TC_CAL_INA_NCH   2

#define TC_CAL_ADC_NCH   8

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

/* Load calibration from STORAGE_DOMAIN_CALIBRATION.
 * Falls back to factory defaults (passthrough for all channels) if storage
 * is empty or corrupt.  Safe to call multiple times. */
int tc_cal_load(void);

/* Persist current in-RAM calibration to STORAGE_DOMAIN_CALIBRATION. */
int tc_cal_save(void);

/* Reset in-RAM calibration to factory defaults (passthrough; VDUT uncalibrated).
 * Does NOT save — call tc_cal_save() explicitly. */
void tc_cal_reset(void);

/* ── VDUT PWM DAC ────────────────────────────────────────────────────────── *
 *
 * Model: V_mv = slope_mv_per_pct × duty_pct + intercept_mv  (inverting)
 * slope=0 means uncalibrated — tc_cal_vdut_duty_for_mv() returns -1.
 */

void tc_cal_get_vdut(int *slope_out, int *intercept_out);
void tc_cal_set_vdut(int slope_mv_per_pct, int intercept_mv);

/* Compute duty_pct for target_mv.  Returns -1 if slope==0 (uncalibrated).
 * Result is clamped to [1, 99]. */
int  tc_cal_vdut_duty_for_mv(int target_mv);

/* ── INA219 (ch 0 or 1) ──────────────────────────────────────────────────── */

/* Voltage channel (Vbus readings in mV) */
void tc_cal_get_ina_v(int ch, float *gain_out, int *offset_mv_out);
void tc_cal_set_ina_v(int ch, float gain, int offset_mv);

/* Current channel (readings in mA) */
void tc_cal_get_ina_i(int ch, float *gain_out, int *offset_ma_out);
void tc_cal_set_ina_i(int ch, float gain, int offset_ma);

/* Apply calibration — returns corrected value */
int  tc_cal_apply_ina_v(int ch, int raw_mv);
int  tc_cal_apply_ina_i(int ch, int raw_ma);

/* ── ADC128D818 (ch 0–7) ─────────────────────────────────────────────────── */

void tc_cal_get_adc(int ch, float *gain_out, int *offset_mv_out);
void tc_cal_set_adc(int ch, float gain, int offset_mv);

/* Apply calibration — returns corrected value in mV */
int  tc_cal_apply_adc(int ch, int raw_mv);

/* ── Console ─────────────────────────────────────────────────────────────── */

void register_cal_commands(void);

#ifdef __cplusplus
}
#endif
