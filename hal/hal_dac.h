/**
 * @file hal_dac.h
 * @brief Digital-to-Analog Converter Hardware Abstraction Layer
 *
 * Controls voltage output to DUT (via adjustable regulator).
 * Implementations: hal_dac_esp32.c, hal_dac_mock.c
 */

#ifndef HAL_DAC_H
#define HAL_DAC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Type Definitions ==================== */

typedef enum {
    HAL_DAC_RESOLUTION_8BIT = 8,
    HAL_DAC_RESOLUTION_10BIT = 10,
    HAL_DAC_RESOLUTION_12BIT = 12
} HAL_DAC_Resolution_t;

typedef enum {
    HAL_DAC_CHANNEL_0 = 0,
    HAL_DAC_CHANNEL_1 = 1
} HAL_DAC_Channel_t;

/* ==================== Core DAC Operations ==================== */

/**
 * @brief Initialize DAC subsystem
 *
 * @param resolution DAC resolution (8/10/12 bit)
 *
 * @return 0 on success, -1 on error
 */
int HAL_DAC_Init(HAL_DAC_Resolution_t resolution);

/**
 * @brief Write raw DAC value
 *
 * @param channel DAC channel (0-1)
 * @param value Raw DAC value (resolution-dependent)
 *
 * @return 0 on success, -1 on error
 */
int HAL_DAC_Write(HAL_DAC_Channel_t channel, uint16_t value);

/**
 * @brief Write voltage to DAC (convenience)
 *
 * Converts voltage in mV to raw DAC value based on scaling.
 *
 * @param channel DAC channel
 * @param voltage_mv Voltage in millivolts (e.g., 3300 for 3.3V)
 * @param vref_mv Reference voltage in mV
 *
 * @return 0 on success, -1 on error
 */
int HAL_DAC_WriteMillivolts(HAL_DAC_Channel_t channel, int32_t voltage_mv, uint16_t vref_mv);

/**
 * @brief Get current DAC resolution
 *
 * @return Resolution in bits (8, 10, 12), or 0 on error
 */
uint8_t HAL_DAC_GetResolution(void);

/* ==================== Advanced Operations ==================== */

/**
 * @brief Start DAC waveform generation (if supported)
 *
 * @param channel DAC channel
 * @param frequency_hz Frequency in Hz
 *
 * @return 0 on success, -1 on error
 */
int HAL_DAC_StartWaveform(HAL_DAC_Channel_t channel, uint16_t frequency_hz);

/**
 * @brief Stop DAC waveform generation
 *
 * @param channel DAC channel
 *
 * @return 0 on success, -1 on error
 */
int HAL_DAC_StopWaveform(HAL_DAC_Channel_t channel);

#ifdef __cplusplus
}
#endif

#endif /* HAL_DAC_H */
