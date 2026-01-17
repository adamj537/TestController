/**
 * @file bsp_g3_tc.c
 * @brief G3 Test Controller Board Support Package Implementation
 *
 * Implements power rail control and board initialization for G3 TC.
 */

#include "bsp_g3_tc.h"
#include "../hal/hal_gpio.h"
#include "../hal/hal_system.h"

/* ==================== Power Rail Mapping ==================== */

/**
 * Power rail GPIO mapping for DUT power domains.
 * Each rail has an enable GPIO pin connected to the DUT.
 */
static const struct {
    HAL_GPIO_Port_t port;
    uint8_t pin;
    bool enabled;
} power_rail_map[] = {
    [BSP_POWER_RAIL_VIN_1] = {HAL_GPIO_PORT_B, 10, false},   /* VIN_1 enable */
    [BSP_POWER_RAIL_VIN_2] = {HAL_GPIO_PORT_B, 12, false},   /* VIN_2 enable */
    [BSP_POWER_RAIL_3V]    = {HAL_GPIO_PORT_A, 8,  false},   /* 3V enable */
    [BSP_POWER_RAIL_5V]    = {HAL_GPIO_PORT_A, 9,  false},   /* 5V enable */
};

static bool bsp_initialized = false;

/* ==================== Power Rail Control ==================== */

int BSP_PowerRailEnable(BSP_PowerRail_t rail) {
    if (rail < 0 || rail >= 4) return -1;

    const struct {
        HAL_GPIO_Port_t port;
        uint8_t pin;
    } *rail_config = (const void *)&power_rail_map[rail];

    /* Initialize GPIO if not already done */
    if (HAL_GPIO_Init(rail_config->port, rail_config->pin, HAL_GPIO_MODE_OUTPUT) != 0) {
        return -1;
    }

    /* Enable the power rail */
    if (HAL_GPIO_WritePin(rail_config->port, rail_config->pin, HAL_GPIO_PIN_SET) != 0) {
        return -1;
    }

    ((bool *)&power_rail_map[rail].enabled)[0] = true;
    return 0;
}

int BSP_PowerRailDisable(BSP_PowerRail_t rail) {
    if (rail < 0 || rail >= 4) return -1;

    const struct {
        HAL_GPIO_Port_t port;
        uint8_t pin;
    } *rail_config = (const void *)&power_rail_map[rail];

    /* Disable the power rail */
    if (HAL_GPIO_WritePin(rail_config->port, rail_config->pin, HAL_GPIO_PIN_RESET) != 0) {
        return -1;
    }

    ((bool *)&power_rail_map[rail].enabled)[0] = false;
    return 0;
}

int BSP_PowerRailIsEnabled(BSP_PowerRail_t rail) {
    if (rail < 0 || rail >= 4) return -1;

    /* Read current state from GPIO */
    const struct {
        HAL_GPIO_Port_t port;
        uint8_t pin;
    } *rail_config = (const void *)&power_rail_map[rail];

    int state = HAL_GPIO_ReadPin(rail_config->port, rail_config->pin);
    return (state == 1) ? 1 : 0;
}

/* ==================== Initialization ==================== */

int BSP_G3_TC_Init(void) {
    if (bsp_initialized) return 0;

    /* Initialize system */
    if (HAL_System_Init() != 0) return -1;

    /* Initialize GPIO subsystem */
    if (HAL_GPIO_SystemInit() != 0) return -1;

    /* Initialize all power rail GPIO pins as outputs */
    for (int i = 0; i < 4; i++) {
        const struct {
            HAL_GPIO_Port_t port;
            uint8_t pin;
        } *rail_config = (const void *)&power_rail_map[i];

        if (HAL_GPIO_Init(rail_config->port, rail_config->pin, HAL_GPIO_MODE_OUTPUT) != 0) {
            return -1;
        }

        /* Disable all power rails by default */
        if (HAL_GPIO_WritePin(rail_config->port, rail_config->pin, HAL_GPIO_PIN_RESET) != 0) {
            return -1;
        }
    }

    bsp_initialized = true;
    return 0;
}
