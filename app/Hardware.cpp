/**
 * @file Hardware.cpp
 * @brief G3 Test Controller Hardware Implementation
 *
 * Implements HAL-based hardware interface for voltage control, current measurement,
 * signal stimulus/measurement, and branch relay control.
 */

#include "Hardware.h"
#include "../hal/hal_gpio.h"
#include "../hal/hal_adc.h"
#include "../hal/hal_dac.h"
#include "../hal/hal_system.h"
#include "../bsp/bsp_g3_tc.h"

/* ==================== Configuration ==================== */

#define VREF_MV                  3300      /* 3.3V reference */
#define CURRENT_SENSE_MULTIPLIER 1000      /* mA per ADC reading */

/* ==================== Static State ==================== */

static struct {
    bool initialized;
    uint8_t current_branch;  /* 0 = none, 1-4 = branch number */
} hardware_state = {false, 0};

/* ==================== Initialization ==================== */

int begin(void) {
    if (hardware_state.initialized) return 0;

    /* Initialize GPIO subsystem */
    if (HAL_GPIO_SystemInit() != 0) return -1;

    /* Initialize DAC for voltage and stimulus control */
    if (HAL_DAC_Init(HAL_DAC_RESOLUTION_12BIT) != 0) return -1;

    /* Initialize ADC for voltage, current, and response measurement */
    if (HAL_ADC_Init(HAL_ADC_UNIT_INTERNAL, HAL_ADC_RESOLUTION_12BIT) != 0) return -1;

    /* Configure ADC channels */
    if (HAL_ADC_ConfigChannel(HAL_ADC_UNIT_INTERNAL, BSP_ADC_VOUT_FEEDBACK) != 0) return -1;
    if (HAL_ADC_ConfigChannel(HAL_ADC_UNIT_INTERNAL, BSP_ADC_CURRENT_SENSE) != 0) return -1;
    if (HAL_ADC_ConfigChannel(HAL_ADC_UNIT_INTERNAL, BSP_ADC_RESPONSE_CHANNEL) != 0) return -1;

    /* Initialize GPIO pins for branch relays */
    if (HAL_GPIO_Init(BSP_BRANCH_1_ENABLE_PORT, BSP_BRANCH_1_ENABLE_PIN, HAL_GPIO_MODE_OUTPUT) != 0) return -1;
    if (HAL_GPIO_Init(BSP_BRANCH_2_ENABLE_PORT, BSP_BRANCH_2_ENABLE_PIN, HAL_GPIO_MODE_OUTPUT) != 0) return -1;
    if (HAL_GPIO_Init(BSP_BRANCH_3_ENABLE_PORT, BSP_BRANCH_3_ENABLE_PIN, HAL_GPIO_MODE_OUTPUT) != 0) return -1;
    if (HAL_GPIO_Init(BSP_BRANCH_4_ENABLE_PORT, BSP_BRANCH_4_ENABLE_PIN, HAL_GPIO_MODE_OUTPUT) != 0) return -1;

    /* Initialize GPIO pins for status LEDs */
    if (HAL_GPIO_Init(BSP_STATUS_LED_PORT, BSP_STATUS_LED_PIN, HAL_GPIO_MODE_OUTPUT) != 0) return -1;
    if (HAL_GPIO_Init(BSP_ERROR_LED_PORT, BSP_ERROR_LED_PIN, HAL_GPIO_MODE_OUTPUT) != 0) return -1;

    /* Disable all branches initially (inline — can't call deselectAllBranches() before initialized=true) */
    if (HAL_GPIO_WritePin(BSP_BRANCH_1_ENABLE_PORT, BSP_BRANCH_1_ENABLE_PIN, HAL_GPIO_PIN_RESET) != 0) return -1;
    if (HAL_GPIO_WritePin(BSP_BRANCH_2_ENABLE_PORT, BSP_BRANCH_2_ENABLE_PIN, HAL_GPIO_PIN_RESET) != 0) return -1;
    if (HAL_GPIO_WritePin(BSP_BRANCH_3_ENABLE_PORT, BSP_BRANCH_3_ENABLE_PIN, HAL_GPIO_PIN_RESET) != 0) return -1;
    if (HAL_GPIO_WritePin(BSP_BRANCH_4_ENABLE_PORT, BSP_BRANCH_4_ENABLE_PIN, HAL_GPIO_PIN_RESET) != 0) return -1;

    /* Turn off status LEDs */
    if (HAL_GPIO_WritePin(BSP_STATUS_LED_PORT, BSP_STATUS_LED_PIN, HAL_GPIO_PIN_RESET) != 0) return -1;
    if (HAL_GPIO_WritePin(BSP_ERROR_LED_PORT, BSP_ERROR_LED_PIN, HAL_GPIO_PIN_RESET) != 0) return -1;

    /* Set initial voltage to safe level (0V) */
    if (HAL_DAC_WriteMillivolts(BSP_DAC_VOLTAGE_CHANNEL, 0, VREF_MV) != 0) return -1;
    if (HAL_DAC_WriteMillivolts(BSP_DAC_STIMULUS_CHANNEL, 0, VREF_MV) != 0) return -1;

    hardware_state.initialized = true;
    return 0;
}

/* ==================== Voltage Control ==================== */

int adjustVoltage(int32_t voltage_mv) {
    if (!hardware_state.initialized) return -1;
    if (voltage_mv < 0 || voltage_mv > VREF_MV) return -1;

    return HAL_DAC_WriteMillivolts(BSP_DAC_VOLTAGE_CHANNEL, voltage_mv, VREF_MV);
}

int32_t readVoltage(void) {
    if (!hardware_state.initialized) return -1;

    return HAL_ADC_ReadMillivolts(HAL_ADC_UNIT_INTERNAL, BSP_ADC_VOUT_FEEDBACK, VREF_MV);
}

/* ==================== Current Measurement ==================== */

int32_t readCurrent(void) {
    if (!hardware_state.initialized) return -1;

    int32_t adc_reading = HAL_ADC_ReadMillivolts(HAL_ADC_UNIT_INTERNAL, BSP_ADC_CURRENT_SENSE, VREF_MV);
    if (adc_reading < 0) return -1;

    /* Convert ADC millivolts to current in milliamps via sense resistor */
    /* Assuming sense resistor circuit: 1mV = 1mA (typical configuration) */
    return adc_reading;  /* Direct conversion for standard sense resistor */
}

/* ==================== Signal Stimulus & Measurement ==================== */

int setStimulusSignal(int32_t signal_mv) {
    if (!hardware_state.initialized) return -1;
    if (signal_mv < 0 || signal_mv > VREF_MV) return -1;

    return HAL_DAC_WriteMillivolts(BSP_DAC_STIMULUS_CHANNEL, signal_mv, VREF_MV);
}

int32_t readResponseSignal(void) {
    if (!hardware_state.initialized) return -1;

    return HAL_ADC_ReadMillivolts(HAL_ADC_UNIT_INTERNAL, BSP_ADC_RESPONSE_CHANNEL, VREF_MV);
}

/* ==================== Branch Control ==================== */

int selectBranch(uint8_t branch_num) {
    if (!hardware_state.initialized) return -1;
    if (branch_num < 1 || branch_num > 4) return -1;

    /* Disable currently active branch */
    if (hardware_state.current_branch != 0) {
        deselectAllBranches();
    }

    /* Enable the requested branch */
    int result = -1;
    switch (branch_num) {
        case 1:
            result = HAL_GPIO_WritePin(BSP_BRANCH_1_ENABLE_PORT, BSP_BRANCH_1_ENABLE_PIN, HAL_GPIO_PIN_SET);
            break;
        case 2:
            result = HAL_GPIO_WritePin(BSP_BRANCH_2_ENABLE_PORT, BSP_BRANCH_2_ENABLE_PIN, HAL_GPIO_PIN_SET);
            break;
        case 3:
            result = HAL_GPIO_WritePin(BSP_BRANCH_3_ENABLE_PORT, BSP_BRANCH_3_ENABLE_PIN, HAL_GPIO_PIN_SET);
            break;
        case 4:
            result = HAL_GPIO_WritePin(BSP_BRANCH_4_ENABLE_PORT, BSP_BRANCH_4_ENABLE_PIN, HAL_GPIO_PIN_SET);
            break;
        default:
            return -1;
    }

    if (result == 0) {
        hardware_state.current_branch = branch_num;
    }

    return result;
}

int deselectAllBranches(void) {
    if (!hardware_state.initialized) return -1;

    int result = 0;

    if (HAL_GPIO_WritePin(BSP_BRANCH_1_ENABLE_PORT, BSP_BRANCH_1_ENABLE_PIN, HAL_GPIO_PIN_RESET) != 0) result = -1;
    if (HAL_GPIO_WritePin(BSP_BRANCH_2_ENABLE_PORT, BSP_BRANCH_2_ENABLE_PIN, HAL_GPIO_PIN_RESET) != 0) result = -1;
    if (HAL_GPIO_WritePin(BSP_BRANCH_3_ENABLE_PORT, BSP_BRANCH_3_ENABLE_PIN, HAL_GPIO_PIN_RESET) != 0) result = -1;
    if (HAL_GPIO_WritePin(BSP_BRANCH_4_ENABLE_PORT, BSP_BRANCH_4_ENABLE_PIN, HAL_GPIO_PIN_RESET) != 0) result = -1;

    if (result == 0) {
        hardware_state.current_branch = 0;
    }

    return result;
}

/* ==================== Power Management ==================== */

int powerRailEnable(int rail) {
    if (!hardware_state.initialized) return -1;

    return BSP_PowerRailEnable((BSP_PowerRail_t)rail);
}

int powerRailDisable(int rail) {
    if (!hardware_state.initialized) return -1;

    return BSP_PowerRailDisable((BSP_PowerRail_t)rail);
}

int powerRailIsEnabled(int rail) {
    if (!hardware_state.initialized) return -1;

    return BSP_PowerRailIsEnabled((BSP_PowerRail_t)rail);
}

/* ==================== Status & Diagnostics ==================== */

int heartbeat(void) {
    if (!hardware_state.initialized) return -1;

    return HAL_GPIO_TogglePin(BSP_STATUS_LED_PORT, BSP_STATUS_LED_PIN);
}

int setErrorLed(bool on) {
    if (!hardware_state.initialized) return -1;

    HAL_GPIO_PinState_t state = on ? HAL_GPIO_PIN_SET : HAL_GPIO_PIN_RESET;
    return HAL_GPIO_WritePin(BSP_ERROR_LED_PORT, BSP_ERROR_LED_PIN, state);
}

/* ==================== Test Helper ==================== */

#ifdef NATIVE_BUILD
/**
 * @brief Reset hardware state for test isolation.
 *
 * Only compiled in native/mock builds. Allows each unit test to start
 * with hardware_state.initialized = false without needing a real power cycle.
 */
void Hardware_Mock_Reset(void) {
    hardware_state.initialized = false;
    hardware_state.current_branch = 0;
}
#endif

#ifdef __cplusplus
}
#endif
