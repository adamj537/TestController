/**
 * @file hal_adc_mock.c
 * @brief ADC Mock Implementation for Unit Testing
 *
 * Simulates ADC readings with configurable return values.
 */

#include "../hal_adc.h"
#include <string.h>

/* ==================== Mock State ==================== */

static struct {
    HAL_ADC_Resolution_t resolution;
    int32_t channel_values[2][16];  /* [unit][channel] */
    bool initialized;
} adc_mock = {0};

/* ==================== Implementation ==================== */

int HAL_ADC_Init(HAL_ADC_Unit_t unit, HAL_ADC_Resolution_t resolution) {
    if (unit > HAL_ADC_UNIT_EXTERNAL) return -1;

    adc_mock.resolution = resolution;
    memset(adc_mock.channel_values, 0, sizeof(adc_mock.channel_values));
    adc_mock.initialized = true;
    return 0;
}

int HAL_ADC_ConfigChannel(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel) {
    if (unit > HAL_ADC_UNIT_EXTERNAL || channel > 15) return -1;
    return 0;
}

int32_t HAL_ADC_Read(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel) {
    if (!adc_mock.initialized || unit > HAL_ADC_UNIT_EXTERNAL || channel > 15) {
        return -1;
    }

    return adc_mock.channel_values[unit][channel];
}

int32_t HAL_ADC_ReadMillivolts(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel, uint16_t vref_mv) {
    int32_t raw = HAL_ADC_Read(unit, channel);
    if (raw < 0) return -1;

    /* Convert raw to mV based on resolution */
    uint32_t max_raw = (1 << adc_mock.resolution) - 1;
    return (raw * vref_mv) / max_raw;
}

uint8_t HAL_ADC_GetResolution(HAL_ADC_Unit_t unit) {
    if (!adc_mock.initialized) return 0;
    return adc_mock.resolution;
}

int HAL_ADC_SetSamplingTime(HAL_ADC_Unit_t unit, uint16_t ms) {
    /* Mock ignores sampling time */
    return 0;
}

int HAL_ADC_StartContinuous(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel) {
    if (unit > HAL_ADC_UNIT_EXTERNAL || channel > 15) return -1;
    return 0;
}

int HAL_ADC_StopContinuous(HAL_ADC_Unit_t unit) {
    if (unit > HAL_ADC_UNIT_EXTERNAL) return -1;
    return 0;
}

/* ==================== Mock Access Functions (for testing) ==================== */

/**
 * @brief Set mock ADC return value
 */
void HAL_ADC_Mock_SetChannelValue(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel, int32_t value) {
    if (unit <= HAL_ADC_UNIT_EXTERNAL && channel <= 15) {
        adc_mock.channel_values[unit][channel] = value;
    }
}

/**
 * @brief Reset mock state
 */
void HAL_ADC_Mock_Reset(void) {
    memset(&adc_mock, 0, sizeof(adc_mock));
}
