/**
 * @file hal_dac_esp32.c
 * @brief ESP32-specific DAC HAL implementation
 *
 * Uses ESP-IDF DAC driver to implement platform-independent DAC interface.
 * ESP32 has 2 12-bit DAC channels (GPIO25, GPIO26).
 */

#include "../hal_dac.h"
#include "driver/dac.h"
#include <string.h>

/* ==================== Configuration ==================== */

typedef struct {
    bool initialized;
    HAL_DAC_Resolution_t resolution;
    uint16_t vref_mv;
} DAC_State_t;

static DAC_State_t dac_state = {false, HAL_DAC_RESOLUTION_12BIT, 3300};

/* ==================== Initialization ==================== */

int HAL_DAC_Init(HAL_DAC_Resolution_t resolution) {
    if (dac_state.initialized) return 0;

    /* Validate resolution (ESP32 only supports 8-bit mode natively) */
    switch (resolution) {
        case HAL_DAC_RESOLUTION_8BIT:
        case HAL_DAC_RESOLUTION_10BIT:
        case HAL_DAC_RESOLUTION_12BIT:
            break;
        default:
            return -1;
    }

    /* Enable DAC */
    dac_output_enable(DAC_CHANNEL_1);
    dac_output_enable(DAC_CHANNEL_2);

    dac_state.resolution = resolution;
    dac_state.initialized = true;

    return 0;
}

/* ==================== DAC Output ==================== */

int HAL_DAC_Write(HAL_DAC_Channel_t channel, uint16_t value) {
    if (!dac_state.initialized) return -1;

    if (channel >= 2) return -1;

    /* Map HAL channel to ESP32 DAC channel */
    dac_channel_t dac_ch = (channel == HAL_DAC_CHANNEL_0) ? DAC_CHANNEL_1 : DAC_CHANNEL_2;

    /* Clamp value to 8-bit range (ESP32 DAC uses 0-255) */
    uint8_t dac_value = 0;

    switch (dac_state.resolution) {
        case HAL_DAC_RESOLUTION_8BIT:
            dac_value = (uint8_t)(value & 0xFF);
            break;
        case HAL_DAC_RESOLUTION_10BIT:
            /* Map 10-bit (0-1023) to 8-bit (0-255) */
            dac_value = (uint8_t)((value * 255) / 1023);
            break;
        case HAL_DAC_RESOLUTION_12BIT:
            /* Map 12-bit (0-4095) to 8-bit (0-255) */
            dac_value = (uint8_t)((value * 255) / 4095);
            break;
        default:
            return -1;
    }

    dac_output_voltage(dac_ch, dac_value);
    return 0;
}

int HAL_DAC_WriteMillivolts(HAL_DAC_Channel_t channel, int32_t voltage_mv, uint16_t vref_mv) {
    if (!dac_state.initialized) return -1;

    if (channel >= 2) return -1;

    /* Clamp voltage to valid range */
    if (voltage_mv < 0) voltage_mv = 0;
    if (voltage_mv > vref_mv) voltage_mv = vref_mv;

    /* Convert millivolts to raw value based on resolution */
    uint16_t raw_value = 0;

    switch (dac_state.resolution) {
        case HAL_DAC_RESOLUTION_8BIT:
            raw_value = (uint16_t)((voltage_mv * 255) / vref_mv);
            break;
        case HAL_DAC_RESOLUTION_10BIT:
            raw_value = (uint16_t)((voltage_mv * 1023) / vref_mv);
            break;
        case HAL_DAC_RESOLUTION_12BIT:
            raw_value = (uint16_t)((voltage_mv * 4095) / vref_mv);
            break;
        default:
            return -1;
    }

    return HAL_DAC_Write(channel, raw_value);
}

/* ==================== Status ==================== */

uint8_t HAL_DAC_GetResolution(void) {
    if (!dac_state.initialized) return 0;

    switch (dac_state.resolution) {
        case HAL_DAC_RESOLUTION_8BIT:
            return 8;
        case HAL_DAC_RESOLUTION_10BIT:
            return 10;
        case HAL_DAC_RESOLUTION_12BIT:
            return 12;
        default:
            return 0;
    }
}

/* ==================== Advanced Operations ==================== */

int HAL_DAC_StartWaveform(HAL_DAC_Channel_t channel, uint16_t frequency_hz) {
    if (!dac_state.initialized) return -1;

    if (channel >= 2) return -1;

    /* Waveform generation not implemented in this basic version */
    return -1;
}

int HAL_DAC_StopWaveform(HAL_DAC_Channel_t channel) {
    if (!dac_state.initialized) return -1;

    if (channel >= 2) return -1;

    return 0;
}
