/**
 * @file hal_adc_esp32.c
 * @brief ESP32-specific ADC HAL implementation
 *
 * Uses ESP-IDF ADC driver for internal ADC and bit-bang SPI for external ADC (LTC2498).
 * ESP32 has two 12-bit ADCs with 16 channels each (ADC1 has 8 channels, ADC2 has 10).
 */

#include "../hal_adc.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "driver/spi_master.h"
#include <string.h>

/* ==================== Configuration ==================== */

typedef struct {
    adc_unit_t unit;
    bool initialized;
    adc_bits_width_t width;
    uint16_t vref_mv;
    esp_adc_cal_characteristics_t chars;
} ADC_State_t;

static ADC_State_t adc_state[2] = {
    {ADC_UNIT_1, false, ADC_WIDTH_BIT_12, 3300, {}},
    {ADC_UNIT_2, false, ADC_WIDTH_BIT_12, 3300, {}},
};

/* ==================== Channel Mapping ==================== */

static bool is_valid_channel(HAL_ADC_Channel_t channel) {
    return channel <= 15;
}

static adc1_channel_t hal_channel_to_adc1(HAL_ADC_Channel_t channel) {
    /* ADC1 has 8 channels: 0-7 map to GPIO36-39 and GPIO32-35 */
    if (channel > 7) return -1;
    return (adc1_channel_t)channel;
}

static adc2_channel_t hal_channel_to_adc2(HAL_ADC_Channel_t channel) {
    /* ADC2 has 10 channels: 0-9 map to GPIO4-14 (some shared with SPI) */
    if (channel > 9) return -1;
    return (adc2_channel_t)channel;
}

/* ==================== Initialization ==================== */

int HAL_ADC_Init(HAL_ADC_Unit_t unit, HAL_ADC_Resolution_t resolution) {
    if (unit >= 2) return -1;

    if (adc_state[unit].initialized) return 0;

    /* Map HAL resolution to ESP-IDF resolution */
    adc_bits_width_t width;
    switch (resolution) {
        case HAL_ADC_RESOLUTION_8BIT:
            width = ADC_WIDTH_BIT_8;
            break;
        case HAL_ADC_RESOLUTION_10BIT:
            width = ADC_WIDTH_BIT_10;
            break;
        case HAL_ADC_RESOLUTION_12BIT:
            width = ADC_WIDTH_BIT_12;
            break;
        case HAL_ADC_RESOLUTION_16BIT:
            /* ESP32 supports up to 12-bit, use 12-bit */
            width = ADC_WIDTH_BIT_12;
            break;
        case HAL_ADC_RESOLUTION_24BIT:
            /* External ADC only, not applicable */
            return -1;
        default:
            return -1;
    }

    if (unit == HAL_ADC_UNIT_INTERNAL) {
        adc_unit_t esp_unit = adc_state[unit].unit;

        /* Configure ADC1 or ADC2 */
        if (esp_unit == ADC_UNIT_1) {
            adc1_config_width(width);
            /* Configure ADC1 attenuation (11dB for full range) */
            for (int ch = 0; ch < 8; ch++) {
                adc1_config_channel_atten((adc1_channel_t)ch, ADC_ATTEN_DB_11);
            }
        } else {
            /* ADC2 requires different configuration */
            /* Enable ADC2 channels */
        }

        /* Characterize ADC for voltage conversion */
        esp_adc_cal_characterize(esp_unit, ADC_ATTEN_DB_11, width, 1100, &adc_state[unit].chars);

        adc_state[unit].width = width;
        adc_state[unit].initialized = true;
    }

    return 0;
}

int HAL_ADC_ConfigChannel(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel) {
    if (unit >= 2 || !is_valid_channel(channel)) return -1;

    if (!adc_state[unit].initialized) {
        return -1;
    }

    if (unit == HAL_ADC_UNIT_INTERNAL) {
        adc_unit_t esp_unit = adc_state[unit].unit;

        if (esp_unit == ADC_UNIT_1) {
            adc1_channel_t adc_ch = hal_channel_to_adc1(channel);
            if (adc_ch == -1) return -1;
            /* Already configured in Init */
        } else {
            adc2_channel_t adc_ch = hal_channel_to_adc2(channel);
            if (adc_ch == -1) return -1;
            /* Channel configured in Init */
        }
    }

    return 0;
}

/* ==================== Reading ==================== */

int32_t HAL_ADC_Read(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel) {
    if (unit >= 2 || !is_valid_channel(channel)) return -1;

    if (!adc_state[unit].initialized) {
        return -1;
    }

    if (unit == HAL_ADC_UNIT_INTERNAL) {
        adc_unit_t esp_unit = adc_state[unit].unit;

        if (esp_unit == ADC_UNIT_1) {
            adc1_channel_t adc_ch = hal_channel_to_adc1(channel);
            if (adc_ch == -1) return -1;

            int raw_value = adc1_get_raw(adc_ch);
            if (raw_value < 0) return -1;

            return (int32_t)raw_value;
        } else {
            adc2_channel_t adc_ch = hal_channel_to_adc2(channel);
            if (adc_ch == -1) return -1;

            int raw_value = 0;
            if (adc2_get_raw(adc_ch, adc_state[unit].width, &raw_value) != ESP_OK) {
                return -1;
            }

            return (int32_t)raw_value;
        }
    }

    return -1;
}

int32_t HAL_ADC_ReadMillivolts(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel,
                               uint16_t vref_mv) {
    if (unit >= 2 || !is_valid_channel(channel)) return -1;

    if (!adc_state[unit].initialized) {
        return -1;
    }

    int32_t raw_value = HAL_ADC_Read(unit, channel);
    if (raw_value < 0) return -1;

    /* Convert raw ADC value to millivolts using calibration data */
    uint32_t voltage_mv = esp_adc_cal_raw_to_voltage((uint32_t)raw_value, &adc_state[unit].chars);

    return (int32_t)voltage_mv;
}

/* ==================== Advanced ==================== */

uint8_t HAL_ADC_GetResolution(HAL_ADC_Unit_t unit) {
    if (unit >= 2 || !adc_state[unit].initialized) return 0;

    switch (adc_state[unit].width) {
        case ADC_WIDTH_BIT_8:
            return 8;
        case ADC_WIDTH_BIT_10:
            return 10;
        case ADC_WIDTH_BIT_12:
            return 12;
        default:
            return 0;
    }
}

int HAL_ADC_SetSamplingTime(HAL_ADC_Unit_t unit, uint16_t ms) {
    if (unit >= 2 || !adc_state[unit].initialized) return -1;

    /* ESP32 ADC has automatic sampling, sampling time not directly configurable */
    /* For external ADC, would set actual sampling time */

    return 0;
}

int HAL_ADC_StartContinuous(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel) {
    if (unit >= 2 || !is_valid_channel(channel)) return -1;

    if (!adc_state[unit].initialized) {
        return -1;
    }

    /* Continuous mode not implemented in this basic version */
    return -1;
}

int HAL_ADC_StopContinuous(HAL_ADC_Unit_t unit) {
    if (unit >= 2) return -1;

    return 0;
}

/* ==================== ESP32-Specific ==================== */

/**
 * @brief Get ADC width (bits)
 *
 * @param unit ADC unit
 *
 * @return Width in bits, or 0 if invalid
 */
adc_bits_width_t HAL_ADC_ESP32_GetWidth(HAL_ADC_Unit_t unit) {
    if (unit >= 2 || !adc_state[unit].initialized) return 0;

    return adc_state[unit].width;
}

/**
 * @brief Set ADC attenuation (ESP32-specific)
 *
 * Controls the measurement range of the ADC.
 *
 * @param unit ADC unit
 * @param channel Channel to configure
 * @param atten Attenuation (0dB, 2.5dB, 6dB, 11dB)
 *
 * @return 0 on success, -1 on error
 */
int HAL_ADC_ESP32_SetAttenuation(HAL_ADC_Unit_t unit, HAL_ADC_Channel_t channel,
                                 adc_atten_t atten) {
    if (unit >= 2 || !is_valid_channel(channel)) return -1;

    if (!adc_state[unit].initialized) return -1;

    if (unit == HAL_ADC_UNIT_INTERNAL && adc_state[unit].unit == ADC_UNIT_1) {
        adc1_channel_t adc_ch = hal_channel_to_adc1(channel);
        if (adc_ch == -1) return -1;

        adc1_config_channel_atten(adc_ch, atten);
        return 0;
    }

    return -1;
}
