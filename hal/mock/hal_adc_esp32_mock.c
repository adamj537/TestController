/**
 * @file hal_adc_esp32_mock.c
 * @brief Mock ESP32 ADC HAL for Linux testing
 *
 * Simulates ADC measurements without ESP-IDF dependencies.
 * Allows Hardware module testing on Linux.
 */

#include "../hal_adc.h"
#include <string.h>
#include <stdio.h>

/* ==================== Mock State ==================== */

typedef struct {
    bool initialized;
    HAL_ADC_Resolution_t resolution;
    uint16_t vref_mv;
    uint16_t channel_values[16];  /* Simulated raw values for each channel */
} ADC_Mock_t;

static ADC_Mock_t adc_mock[2] = {0};

/* Mock debug logging */
#ifdef DEBUG
#define ADC_LOG(fmt, ...) printf("[ADC] " fmt "\n", ##__VA_ARGS__)
#else
#define ADC_LOG(fmt, ...) do {} while(0)
#endif

/* ==================== Initialization ==================== */

int HAL_ADC_Init(HAL_ADC_Unit_t unit, HAL_ADC_Resolution_t resolution) {
    if (unit >= 2) return -1;

    if (adc_mock[unit].initialized) return 0;

    adc_mock[unit].resolution = resolution;
    adc_mock[unit].vref_mv = 3300;
    adc_mock[unit].initialized = true;

    /* Initialize with some default values (mid-range) */
    for (int i = 0; i < 16; i++) {
        switch (resolution) {
            case HAL_ADC_RESOLUTION_8BIT:
                adc_mock[unit].channel_values[i] = 128;
                break;
            case HAL_ADC_RESOLUTION_10BIT:
                adc_mock[unit].channel_values[i] = 512;
                break;
            case HAL_ADC_RESOLUTION_12BIT:
                adc_mock[unit].channel_values[i] = 2048;
                break;
            case HAL_ADC_RESOLUTION_16BIT:
                adc_mock[unit].channel_values[i] = 32768;
                break;
            case HAL_ADC_RESOLUTION_24BIT:
                adc_mock[unit].channel_values[i] = 0;
                break;
        }
    }

    ADC_LOG("Init unit %d, resolution %d-bit", unit,
            (resolution == HAL_ADC_RESOLUTION_8BIT ? 8 :
             resolution == HAL_ADC_RESOLUTION_10BIT ? 10 :
             resolution == HAL_ADC_RESOLUTION_12BIT ? 12 : 0));
    return 0;
}

int HAL_ADC_ConfigChannel(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel) {
    if (unit >= 2 || channel >= 16) return -1;

    if (!adc_mock[unit].initialized) return -1;

    ADC_LOG("Config unit %d channel %d", unit, channel);
    return 0;
}

/* ==================== Reading ==================== */

int32_t HAL_ADC_Read(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel) {
    if (unit >= 2 || channel >= 16) return -1;

    if (!adc_mock[unit].initialized) return -1;

    int32_t value = adc_mock[unit].channel_values[channel];
    ADC_LOG("Read unit %d channel %d = %d", unit, channel, value);
    return value;
}

int32_t HAL_ADC_ReadMillivolts(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel,
                               uint16_t vref_mv) {
    if (unit >= 2 || channel >= 16) return -1;

    if (!adc_mock[unit].initialized) return -1;

    int32_t raw = adc_mock[unit].channel_values[channel];

    /* Convert raw to millivolts based on resolution */
    uint32_t max_raw = 0;
    switch (adc_mock[unit].resolution) {
        case HAL_ADC_RESOLUTION_8BIT:
            max_raw = 255;
            break;
        case HAL_ADC_RESOLUTION_10BIT:
            max_raw = 1023;
            break;
        case HAL_ADC_RESOLUTION_12BIT:
            max_raw = 4095;
            break;
        case HAL_ADC_RESOLUTION_16BIT:
            max_raw = 65535;
            break;
        default:
            return -1;
    }

    int32_t voltage_mv = (raw * vref_mv) / max_raw;
    ADC_LOG("ReadMillivolts unit %d channel %d = %d mV", unit, channel, voltage_mv);
    return voltage_mv;
}

/* ==================== Status ==================== */

uint8_t HAL_ADC_GetResolution(HAL_ADC_Unit_t unit) {
    if (unit >= 2 || !adc_mock[unit].initialized) return 0;

    switch (adc_mock[unit].resolution) {
        case HAL_ADC_RESOLUTION_8BIT:
            return 8;
        case HAL_ADC_RESOLUTION_10BIT:
            return 10;
        case HAL_ADC_RESOLUTION_12BIT:
            return 12;
        case HAL_ADC_RESOLUTION_16BIT:
            return 16;
        case HAL_ADC_RESOLUTION_24BIT:
            return 24;
        default:
            return 0;
    }
}

int HAL_ADC_SetSamplingTime(HAL_ADC_Unit_t unit, uint16_t ms) {
    if (unit >= 2) return -1;
    ADC_LOG("SetSamplingTime unit %d = %d ms", unit, ms);
    return 0;
}

int HAL_ADC_StartContinuous(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel) {
    if (unit >= 2 || channel >= 16) return -1;
    ADC_LOG("StartContinuous unit %d channel %d", unit, channel);
    return -1;  /* Not supported in mock */
}

int HAL_ADC_StopContinuous(HAL_ADC_Unit_t unit) {
    if (unit >= 2) return -1;
    ADC_LOG("StopContinuous unit %d", unit);
    return 0;
}

/* ==================== Mock Helpers (for testing) ==================== */

/**
 * @brief Set simulated ADC value for a channel
 */
int HAL_ADC_Mock_SetValue(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel, uint16_t value) {
    if (unit >= 2 || channel >= 16) return -1;

    adc_mock[unit].channel_values[channel] = value;
    ADC_LOG("Mock: Set unit %d channel %d to %d", unit, channel, value);
    return 0;
}

/**
 * @brief Set simulated voltage for a channel
 */
int HAL_ADC_Mock_SetVoltage(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel,
                            uint16_t voltage_mv) {
    if (unit >= 2 || channel >= 16) return -1;

    if (!adc_mock[unit].initialized) return -1;

    /* Convert mV to raw value */
    uint32_t max_raw = 0;
    switch (adc_mock[unit].resolution) {
        case HAL_ADC_RESOLUTION_8BIT:
            max_raw = 255;
            break;
        case HAL_ADC_RESOLUTION_10BIT:
            max_raw = 1023;
            break;
        case HAL_ADC_RESOLUTION_12BIT:
            max_raw = 4095;
            break;
        case HAL_ADC_RESOLUTION_16BIT:
            max_raw = 65535;
            break;
        default:
            return -1;
    }

    uint16_t raw = (voltage_mv * max_raw) / adc_mock[unit].vref_mv;
    adc_mock[unit].channel_values[channel] = raw;

    ADC_LOG("Mock: Set unit %d channel %d to %d mV (raw: %d)", unit, channel, voltage_mv, raw);
    return 0;
}

/**
 * @brief Reset all mock state
 */
void HAL_ADC_Mock_Reset(void) {
    memset(&adc_mock, 0, sizeof(adc_mock));
    ADC_LOG("Reset all ADC mock state");
}
