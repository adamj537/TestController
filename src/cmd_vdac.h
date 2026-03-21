#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void register_vdac_commands(void);

/* ── Selftest API ─────────────────────────────────────────────────────────── */

/* Settling times (empirically measured via MP2315SGJ-Z + 47µF output cap) */
#define VDAC_STEP_SETTLE_MS  1500  /* per duty step change */
#define VDAC_INIT_SETTLE_MS  2000  /* initial ramp from disable */

/* LEDC / regulator control — ch_idx: 0=VDUT1, 1=VDUT2 */
bool vdac_set_duty(int ch_idx, int duty_pct);             /* 0–100 % */
bool vdac_set_voltage(int ch_idx, int voltage_mv);        /* converts via calibration; false if uncalibrated */
bool vdac_set_enable(int ch_idx, bool enable);

/* ADC128D818 helpers — all devices on same bus after HW-012 rework */
bool adc128_ensure_running(void);                /* arm Mode-1 conversions */
bool adc128_read_mon(uint8_t ch, int *rail_mv_out); /* CH6=VDUT1, CH7=VDUT2; 4× divider */

#ifdef __cplusplus
}
#endif
