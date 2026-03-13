/**
 * @file hal_adc.h
 * @brief Analog-to-Digital Converter Hardware Abstraction Layer
 *
 * Supports both internal (STM32-like) and external (LTC2498-like) ADCs.
 * Implementations: hal_adc_esp32.c, hal_adc_mock.c
 */

#ifndef HAL_ADC_H
#define HAL_ADC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Type Definitions ==================== */

typedef enum {
    HAL_ADC_RESOLUTION_8BIT = 8,
    HAL_ADC_RESOLUTION_10BIT = 10,
    HAL_ADC_RESOLUTION_12BIT = 12,
    HAL_ADC_RESOLUTION_16BIT = 16,
    HAL_ADC_RESOLUTION_24BIT = 24
} HAL_ADC_Resolution_t;

typedef enum {
    HAL_ADC_CHANNEL_0 = 0,
    HAL_ADC_CHANNEL_1 = 1,
    HAL_ADC_CHANNEL_2 = 2,
    HAL_ADC_CHANNEL_3 = 3,
    HAL_ADC_CHANNEL_4 = 4,
    HAL_ADC_CHANNEL_5 = 5,
    HAL_ADC_CHANNEL_6 = 6,
    HAL_ADC_CHANNEL_7 = 7,
    HAL_ADC_CHANNEL_8 = 8,
    HAL_ADC_CHANNEL_9 = 9,
    HAL_ADC_CHANNEL_10 = 10,
    HAL_ADC_CHANNEL_11 = 11,
    HAL_ADC_CHANNEL_12 = 12,
    HAL_ADC_CHANNEL_13 = 13,
    HAL_ADC_CHANNEL_14 = 14,
    HAL_ADC_CHANNEL_15 = 15
} HAL_ADC_Channel_t;

typedef enum {
    HAL_ADC_UNIT_INTERNAL = 0,  /* STM32 internal ADC / ESP32 ADC1 */
    HAL_ADC_UNIT_EXTERNAL = 1   /* LTC2498 external ADC */
} HAL_ADC_Unit_t;

/* ==================== Core ADC Operations ==================== */

/**
 * @brief Initialize ADC subsystem
 *
 * @param unit ADC unit (INTERNAL or EXTERNAL)
 * @param resolution Resolution (8/10/12/16/24 bit)
 *
 * @return 0 on success, -1 on error
 */
int HAL_ADC_Init(HAL_ADC_Unit_t unit, HAL_ADC_Resolution_t resolution);

/**
 * @brief Configure ADC channel
 *
 * @param unit ADC unit
 * @param channel Channel number (0-15)
 *
 * @return 0 on success, -1 on error
 */
int HAL_ADC_ConfigChannel(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel);

/**
 * @brief Read ADC channel (synchronous)
 *
 * For external ADC, this may take >50ms due to conversion time.
 *
 * @param unit ADC unit
 * @param channel Channel to read
 *
 * @return Raw ADC value (resolution-dependent), or -1 on error
 */
int32_t HAL_ADC_Read(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel);

/**
 * @brief Read ADC and convert to millivolts
 *
 * Applies calibration and scaling to return physical voltage.
 *
 * @param unit ADC unit
 * @param channel Channel to read
 * @param vref_mv Reference voltage in mV (typically 3300 for 3.3V)
 *
 * @return Voltage in mV, or -1 on error
 */
int32_t HAL_ADC_ReadMillivolts(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel, uint16_t vref_mv);

/**
 * @brief Get current ADC resolution
 *
 * @param unit ADC unit
 *
 * @return Resolution in bits (8, 10, 12, 16, 24), or 0 on error
 */
uint8_t HAL_ADC_GetResolution(HAL_ADC_Unit_t unit);

/* ==================== Advanced Operations ==================== */

/**
 * @brief Set ADC sampling time (external ADC only)
 *
 * For LTC2498, this affects conversion speed and noise.
 *
 * @param unit ADC unit
 * @param ms Sampling time in milliseconds
 *
 * @return 0 on success, -1 on error
 */
int HAL_ADC_SetSamplingTime(HAL_ADC_Unit_t unit, uint16_t ms);

/**
 * @brief Start continuous conversion (if supported)
 *
 * @param unit ADC unit
 * @param channel Channel for continuous conversion
 *
 * @return 0 on success, -1 on error
 */
int HAL_ADC_StartContinuous(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel);

/**
 * @brief Stop continuous conversion
 *
 * @param unit ADC unit
 *
 * @return 0 on success, -1 on error
 */
int HAL_ADC_StopContinuous(HAL_ADC_Unit_t unit);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ADC_H */
