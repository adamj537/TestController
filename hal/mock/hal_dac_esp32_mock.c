/**
 * @file hal_dac_esp32_mock.c
 * @brief Mock ESP32 DAC HAL for Linux testing
 *
 * Simulates DAC output without ESP-IDF dependencies.
 * Allows Hardware module testing on Linux.
 */

#include "../hal_dac.h"
#include <string.h>
#include <stdio.h>

/* ==================== Mock State ==================== */

typedef struct {
    bool initialized;
    HAL_DAC_Resolution_t resolution;
    uint16_t vref_mv;
    uint16_t channel_values[2];  /* Current DAC values */
} DAC_Mock_t;

static DAC_Mock_t dac_mock = {0};

/* Mock debug logging */
#ifdef DEBUG
#define DAC_LOG(fmt, ...) printf("[DAC] " fmt "\n", ##__VA_ARGS__)
#else
#define DAC_LOG(fmt, ...) do {} while(0)
#endif

/* ==================== Initialization ==================== */

int HAL_DAC_Init(HAL_DAC_Resolution_t resolution) {
    if (dac_mock.initialized) return 0;

    dac_mock.resolution = resolution;
    dac_mock.vref_mv = 3300;
    dac_mock.channel_values[0] = 0;
    dac_mock.channel_values[1] = 0;
    dac_mock.initialized = true;

    DAC_LOG("Init, resolution %d-bit",
            (resolution == HAL_DAC_RESOLUTION_8BIT ? 8 :
             resolution == HAL_DAC_RESOLUTION_10BIT ? 10 : 12));
    return 0;
}

/* ==================== DAC Output ==================== */

int HAL_DAC_Write(HAL_DAC_Channel_t channel, uint16_t value) {
    if (!dac_mock.initialized || channel >= 2) return -1;

    /* Clamp to resolution range */
    uint16_t max_value = 0;
    switch (dac_mock.resolution) {
        case HAL_DAC_RESOLUTION_8BIT:
            max_value = 255;
            value = value > 255 ? 255 : value;
            break;
        case HAL_DAC_RESOLUTION_10BIT:
            max_value = 1023;
            value = value > 1023 ? 1023 : value;
            break;
        case HAL_DAC_RESOLUTION_12BIT:
            max_value = 4095;
            value = value > 4095 ? 4095 : value;
            break;
    }

    dac_mock.channel_values[channel] = value;
    DAC_LOG("Write channel %d = %d (max: %d)", channel, value, max_value);
    return 0;
}

int HAL_DAC_WriteMillivolts(HAL_DAC_Channel_t channel, int32_t voltage_mv, uint16_t vref_mv) {
    if (!dac_mock.initialized || channel >= 2) return -1;

    /* Clamp voltage */
    if (voltage_mv < 0) voltage_mv = 0;
    if (voltage_mv > vref_mv) voltage_mv = vref_mv;

    /* Convert mV to raw value */
    uint16_t raw_value = 0;
    uint16_t max_raw = 0;

    switch (dac_mock.resolution) {
        case HAL_DAC_RESOLUTION_8BIT:
            max_raw = 255;
            raw_value = (voltage_mv * 255) / vref_mv;
            break;
        case HAL_DAC_RESOLUTION_10BIT:
            max_raw = 1023;
            raw_value = (voltage_mv * 1023) / vref_mv;
            break;
        case HAL_DAC_RESOLUTION_12BIT:
            max_raw = 4095;
            raw_value = (voltage_mv * 4095) / vref_mv;
            break;
    }

    dac_mock.channel_values[channel] = raw_value;
    DAC_LOG("WriteMillivolts channel %d = %d mV (raw: %d, max: %d)", channel, voltage_mv,
            raw_value, max_raw);
    return 0;
}

/* ==================== Status ==================== */

uint8_t HAL_DAC_GetResolution(void) {
    if (!dac_mock.initialized) return 0;

    switch (dac_mock.resolution) {
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

int HAL_DAC_StartWaveform(HAL_DAC_Channel_t channel, uint16_t frequency_hz) {
    if (!dac_mock.initialized || channel >= 2) return -1;
    DAC_LOG("StartWaveform channel %d at %d Hz", channel, frequency_hz);
    return -1;  /* Not supported */
}

int HAL_DAC_StopWaveform(HAL_DAC_Channel_t channel) {
    if (!dac_mock.initialized || channel >= 2) return -1;
    DAC_LOG("StopWaveform channel %d", channel);
    return 0;
}

/* ==================== Mock Helpers (for testing) ==================== */

/**
 * @brief Get current DAC value for verification
 */
uint16_t HAL_DAC_Mock_GetValue(HAL_DAC_Channel_t channel) {
    return channel < 2 ? dac_mock.channel_values[channel] : 0;
}

/**
 * @brief Get current DAC voltage
 */
int32_t HAL_DAC_Mock_GetVoltage(HAL_DAC_Channel_t channel) {
    if (channel >= 2 || !dac_mock.initialized) return -1;

    uint16_t raw = dac_mock.channel_values[channel];
    uint16_t max_raw = 0;

    switch (dac_mock.resolution) {
        case HAL_DAC_RESOLUTION_8BIT:
            max_raw = 255;
            break;
        case HAL_DAC_RESOLUTION_10BIT:
            max_raw = 1023;
            break;
        case HAL_DAC_RESOLUTION_12BIT:
            max_raw = 4095;
            break;
    }

    return (raw * dac_mock.vref_mv) / max_raw;
}

/**
 * @brief Reset all mock state
 */
void HAL_DAC_Mock_Reset(void) {
    memset(&dac_mock, 0, sizeof(dac_mock));
    DAC_LOG("Reset all DAC mock state");
}
