/**
 * @file hal_dac_mock.c
 * @brief DAC Mock Implementation for Unit Testing
 */

#include "../hal_dac.h"
#include <string.h>

static struct {
    HAL_DAC_Resolution_t resolution;
    uint16_t channel_values[2];
    bool initialized;
} dac_mock = {0};

int HAL_DAC_Init(HAL_DAC_Resolution_t resolution) {
    dac_mock.resolution = resolution;
    memset(dac_mock.channel_values, 0, sizeof(dac_mock.channel_values));
    dac_mock.initialized = true;
    return 0;
}

int HAL_DAC_Write(HAL_DAC_Channel_t channel, uint16_t value) {
    if (!dac_mock.initialized || channel > 1) return -1;
    dac_mock.channel_values[channel] = value;
    return 0;
}

int HAL_DAC_WriteMillivolts(HAL_DAC_Channel_t channel, int32_t voltage_mv, uint16_t vref_mv) {
    if (!dac_mock.initialized || channel > 1) return -1;

    uint32_t max_val = (1 << dac_mock.resolution) - 1;
    uint16_t raw = (voltage_mv * max_val) / vref_mv;

    return HAL_DAC_Write(channel, raw);
}

uint8_t HAL_DAC_GetResolution(void) {
    if (!dac_mock.initialized) return 0;
    return dac_mock.resolution;
}

int HAL_DAC_StartWaveform(HAL_DAC_Channel_t channel, uint16_t frequency_hz) {
    return 0;  /* Mock supports waveforms but doesn't do anything */
}

int HAL_DAC_StopWaveform(HAL_DAC_Channel_t channel) {
    return 0;
}

/* Mock access for testing */
uint16_t HAL_DAC_Mock_GetChannelValue(HAL_DAC_Channel_t channel) {
    if (channel > 1) return 0;
    return dac_mock.channel_values[channel];
}

void HAL_DAC_Mock_Reset(void) {
    memset(&dac_mock, 0, sizeof(dac_mock));
}
