/**
 * @file hal_gpio.h
 * @brief GPIO Hardware Abstraction Layer
 *
 * Platform-independent GPIO interface for TC firmware.
 * Implementations: hal_gpio_esp32.c, hal_gpio_mock.c
 */

#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Type Definitions ==================== */

typedef enum {
    HAL_GPIO_MODE_INPUT = 0,
    HAL_GPIO_MODE_OUTPUT = 1,
    HAL_GPIO_MODE_INPUT_PULLUP = 2,
    HAL_GPIO_MODE_INPUT_PULLDOWN = 3,
    HAL_GPIO_MODE_ANALOG = 4,
    HAL_GPIO_MODE_OPEN_DRAIN = 5
} HAL_GPIO_Mode_t;

typedef enum {
    HAL_GPIO_PIN_RESET = 0,
    HAL_GPIO_PIN_SET = 1
} HAL_GPIO_PinState_t;

typedef enum {
    HAL_GPIO_PORT_A = 0,
    HAL_GPIO_PORT_B = 1,
    HAL_GPIO_PORT_C = 2,
    HAL_GPIO_PORT_D = 3,
    HAL_GPIO_PORT_E = 4,
    HAL_GPIO_PORT_F = 5
} HAL_GPIO_Port_t;

/* ==================== Core GPIO Operations ==================== */

/**
 * @brief Initialize GPIO pin mode
 *
 * @param port GPIO port (HAL_GPIO_PORT_A, etc.)
 * @param pin Pin number (0-15)
 * @param mode Pin mode (INPUT, OUTPUT, etc.)
 *
 * @return 0 on success, -1 on error
 */
int HAL_GPIO_Init(HAL_GPIO_Port_t port, uint8_t pin, HAL_GPIO_Mode_t mode);

/**
 * @brief Set GPIO pin to HIGH or LOW
 *
 * @param port GPIO port
 * @param pin Pin number
 * @param state PIN_SET (1) or PIN_RESET (0)
 *
 * @return 0 on success, -1 on error
 */
int HAL_GPIO_WritePin(HAL_GPIO_Port_t port, uint8_t pin, HAL_GPIO_PinState_t state);

/**
 * @brief Read GPIO pin state
 *
 * @param port GPIO port
 * @param pin Pin number
 *
 * @return PIN_SET (1), PIN_RESET (0), or -1 on error
 */
int HAL_GPIO_ReadPin(HAL_GPIO_Port_t port, uint8_t pin);

/**
 * @brief Toggle GPIO pin state
 *
 * @param port GPIO port
 * @param pin Pin number
 *
 * @return 0 on success, -1 on error
 */
int HAL_GPIO_TogglePin(HAL_GPIO_Port_t port, uint8_t pin);

/**
 * @brief Set multiple pins atomically (port-level write)
 *
 * @param port GPIO port
 * @param mask Bit mask of pins to write
 * @param value Bit values to write
 *
 * @return 0 on success, -1 on error
 */
int HAL_GPIO_WritePortMasked(HAL_GPIO_Port_t port, uint32_t mask, uint32_t value);

/**
 * @brief Read entire port state
 *
 * @param port GPIO port
 *
 * @return 16-bit port value, or -1 on error
 */
uint32_t HAL_GPIO_ReadPort(HAL_GPIO_Port_t port);

/* ==================== Initialization ==================== */

/**
 * @brief Initialize GPIO subsystem
 *
 * Must be called once at startup before using GPIO functions.
 *
 * @return 0 on success, -1 on error
 */
int HAL_GPIO_SystemInit(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPIO_H */
