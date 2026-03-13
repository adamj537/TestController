/**
 * @file bsp_g3_tc.h
 * @brief G3 Test Controller Board Support Package
 *
 * Pin definitions, peripheral mappings, and hardware configuration for G3 TC.
 */

#ifndef BSP_G3_TC_H
#define BSP_G3_TC_H

#include "../hal/hal_gpio.h"
#include "../hal/hal_adc.h"
#include "../hal/hal_dac.h"
#include "../hal/hal_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== DUT UART Interface ==================== */

/**
 * UART for communicating with DUT firmware (FW-2)
 * - Protocol: FW-2 UART command spec (pfw-uart-command-spec.md)
 * - Baudrate: 115200
 * - Configuration: 8N1, no flow control
 * - STM32 pins: PB6 (TX), PB7 (RX)
 * - ESP32 pins: GPIO17 (TX), GPIO16 (RX) [example, adjust to actual]
 */
#define BSP_DUT_UART_PORT           HAL_UART_PORT_1
#define BSP_DUT_UART_BAUDRATE       115200

/* For ESP32, these define the GPIO mapping */
#ifdef ESP_PLATFORM
    #define BSP_DUT_UART_RX_GPIO        GPIO_NUM_16
    #define BSP_DUT_UART_TX_GPIO        GPIO_NUM_17
#endif

/* ==================== Voltage Control (Adjustable Regulator) ==================== */

/**
 * DAC controls the adjustable regulator voltage output to DUT
 * - Regulator input: Programmable via DAC
 * - Regulator output: Monitored via ADC
 */
#define BSP_DAC_VOLTAGE_CHANNEL     HAL_DAC_CHANNEL_0
#define BSP_ADC_VOUT_FEEDBACK       HAL_ADC_CHANNEL_0

/* ==================== Current Measurement ==================== */

/**
 * ADC measures current drawn by DUT (via sense resistor)
 * Typical circuit: Current sense amplifier → ADC
 */
#define BSP_ADC_CURRENT_SENSE       HAL_ADC_CHANNEL_1

/* ==================== Signal Stimulus / Measurement ==================== */

/**
 * DAC for applying stimulus signals (test signals)
 * ADC for measuring DUT output signals
 */
#define BSP_DAC_STIMULUS_CHANNEL    HAL_DAC_CHANNEL_1
#define BSP_ADC_RESPONSE_CHANNEL    HAL_ADC_CHANNEL_2

/* ==================== Branch Relay Control ==================== */

/**
 * Relays select which branch is connected for measurement
 * Each branch has enable/select GPIO pins
 */

#define BSP_BRANCH_1_ENABLE_PORT    HAL_GPIO_PORT_C
#define BSP_BRANCH_1_ENABLE_PIN     9   /* TC_enable equivalent */

#define BSP_BRANCH_2_ENABLE_PORT    HAL_GPIO_PORT_B
#define BSP_BRANCH_2_ENABLE_PIN     11  /* VIN_3_ENA equivalent */

#define BSP_BRANCH_3_ENABLE_PORT    HAL_GPIO_PORT_D
#define BSP_BRANCH_3_ENABLE_PIN     15  /* SS_ENA_B equivalent */

#define BSP_BRANCH_4_ENABLE_PORT    HAL_GPIO_PORT_C
#define BSP_BRANCH_4_ENABLE_PIN     13  /* PUMP_ENA equivalent */

/* ==================== Status/Debug GPIO ==================== */

/**
 * LEDs for operational status
 */
#define BSP_STATUS_LED_PORT         HAL_GPIO_PORT_E
#define BSP_STATUS_LED_PIN          5   /* Blue LED */

#define BSP_ERROR_LED_PORT          HAL_GPIO_PORT_C
#define BSP_ERROR_LED_PIN           7   /* Red LED */

/* ==================== Power Rails ==================== */

/**
 * Power domains on DUT that TC controls
 * (Represented by GPIO enables)
 */

typedef enum {
    BSP_POWER_RAIL_VIN_1,
    BSP_POWER_RAIL_VIN_2,
    BSP_POWER_RAIL_3V,
    BSP_POWER_RAIL_5V
} BSP_PowerRail_t;

/**
 * Enable specific power rail
 * @param rail Power rail to enable
 * @return 0 on success, -1 on error
 */
int BSP_PowerRailEnable(BSP_PowerRail_t rail);

/**
 * Disable specific power rail
 * @param rail Power rail to disable
 * @return 0 on success, -1 on error
 */
int BSP_PowerRailDisable(BSP_PowerRail_t rail);

/**
 * Check if power rail is enabled
 * @param rail Power rail to check
 * @return 1 if enabled, 0 if disabled, -1 on error
 */
int BSP_PowerRailIsEnabled(BSP_PowerRail_t rail);

/* ==================== Initialization ==================== */

/**
 * Initialize G3 TC hardware
 * - Configures all GPIO, ADC, DAC, UART pins
 * - Sets up peripherals for operation
 * - Does NOT start test execution
 *
 * @return 0 on success, -1 on error
 */
int BSP_G3_TC_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_G3_TC_H */
