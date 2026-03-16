/**
 * @file tie_pinmap.h
 * @brief TIE (Tester Interface Board) GPIO pin assignments — G3 fixture layer
 *
 * Defines GPIO numbers for signals routed from the ESP32-S3 through the TCC
 * J15 connector to the G3 TIE board.  Only depends on ESP-IDF driver/gpio.h —
 * safe to include from any source file without pulling in the HAL or BSP
 * abstraction layers.
 *
 * Board-level TCC constants (I2C, SWD) live in tcc_pinmap.h (common layer).
 * This file adds G3-specific MUX channel assignments on top.
 *
 * Pin assignments confirmed during Phase 3 hardware bringup (2026-03):
 * address lines exercised and all 64 MUX channels read successfully with
 * DUT and TIE stacked on TCC.
 */

#ifndef TIE_PINMAP_H
#define TIE_PINMAP_H

/* TCC board-level constants (common layer) */
#include "tcc_pinmap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── BSP_I2C_* aliases (backward-compat shims — prefer TCC_I2C_* in new code) */
#define BSP_I2C_SDA_GPIO         TCC_I2C_SDA_GPIO
#define BSP_I2C_SCL_GPIO         TCC_I2C_SCL_GPIO
#define BSP_I2C_ADC128_SDA_GPIO  TCC_I2C_ADC128_SDA_GPIO
#define BSP_I2C_ADC128_SCL_GPIO  TCC_I2C_ADC128_SCL_GPIO
#define BSP_I2C_SPEED_HZ         TCC_I2C_SPEED_HZ

/* ── TIE analog MUX address lines — G3-specific ────────────────────────────*
 * Four HEF4067BTT 16-channel analog MUXes route DUT signal pins to the     *
 * ADC128D818 CH0–CH3.  Each MUX selects 1-of-16 inputs via address bits    *
 * A0–A3.  /EN pins are hardwired to GND on the TIE (always enabled).       *
 *                                                                           *
 *  MUX0 (U3)  A0=GPIO3  A1=GPIO4  A2=GPIO5  A3=GPIO6  → ADC128D818 CH0   *
 *  MUX1 (U11) A0=GPIO7  A1=GPIO8  A2=GPIO9  A3=GPIO10 → ADC128D818 CH1   *
 *  MUX2 (U12) A0=GPIO11 A1=GPIO12 A2=GPIO13 A3=GPIO14 → ADC128D818 CH2   *
 *  MUX3 (U1)  A0=GPIO39 A1=GPIO40 A2=GPIO41 A3=GPIO42 → ADC128D818 CH3   */

/* MUX0 (U3) → ADC128D818 CH0 */
#define BSP_TIE_MUX0_A0     GPIO_NUM_3
#define BSP_TIE_MUX0_A1     GPIO_NUM_4
#define BSP_TIE_MUX0_A2     GPIO_NUM_5
#define BSP_TIE_MUX0_A3     GPIO_NUM_6

/* MUX1 (U11) → ADC128D818 CH1 */
#define BSP_TIE_MUX1_A0     GPIO_NUM_7
#define BSP_TIE_MUX1_A1     GPIO_NUM_8
#define BSP_TIE_MUX1_A2     GPIO_NUM_9
#define BSP_TIE_MUX1_A3     GPIO_NUM_10

/* MUX2 (U12) → ADC128D818 CH2 */
#define BSP_TIE_MUX2_A0     GPIO_NUM_11
#define BSP_TIE_MUX2_A1     GPIO_NUM_12
#define BSP_TIE_MUX2_A2     GPIO_NUM_13
#define BSP_TIE_MUX2_A3     GPIO_NUM_14

/* MUX3 (U1) → ADC128D818 CH3 */
#define BSP_TIE_MUX3_A0     GPIO_NUM_39
#define BSP_TIE_MUX3_A1     GPIO_NUM_40
#define BSP_TIE_MUX3_A2     GPIO_NUM_41
#define BSP_TIE_MUX3_A3     GPIO_NUM_42

#ifdef __cplusplus
}
#endif

#endif /* TIE_PINMAP_H */
