/**
 * @file hal_gpio_mock.c
 * @brief GPIO Mock Implementation for Unit Testing
 *
 * Simulates GPIO operations in software for off-board testing.
 */

#include "../hal_gpio.h"
#include <string.h>

/* ==================== Mock State ==================== */

static struct {
    struct {
        uint16_t mode_mask;      /* Bit=1 if OUTPUT, 0 if INPUT */
        uint16_t state;          /* Current pin states */
    } port[6];
    bool initialized;
} gpio_mock = {0};

/* ==================== Implementation ==================== */

int HAL_GPIO_Init(HAL_GPIO_Port_t port, uint8_t pin, HAL_GPIO_Mode_t mode) {
    if (port >= 6 || pin >= 16) return -1;

    if (mode == HAL_GPIO_MODE_OUTPUT) {
        gpio_mock.port[port].mode_mask |= (1 << pin);
    } else {
        gpio_mock.port[port].mode_mask &= ~(1 << pin);
    }

    return 0;
}

int HAL_GPIO_WritePin(HAL_GPIO_Port_t port, uint8_t pin, HAL_GPIO_PinState_t state) {
    if (port >= 6 || pin >= 16) return -1;

    if (state == HAL_GPIO_PIN_SET) {
        gpio_mock.port[port].state |= (1 << pin);
    } else {
        gpio_mock.port[port].state &= ~(1 << pin);
    }

    return 0;
}

int HAL_GPIO_ReadPin(HAL_GPIO_Port_t port, uint8_t pin) {
    if (port >= 6 || pin >= 16) return -1;

    return (gpio_mock.port[port].state >> pin) & 1;
}

int HAL_GPIO_TogglePin(HAL_GPIO_Port_t port, uint8_t pin) {
    if (port >= 6 || pin >= 16) return -1;

    gpio_mock.port[port].state ^= (1 << pin);
    return 0;
}

int HAL_GPIO_WritePortMasked(HAL_GPIO_Port_t port, uint32_t mask, uint32_t value) {
    if (port >= 6) return -1;

    gpio_mock.port[port].state = (gpio_mock.port[port].state & ~(mask & 0xFFFF)) |
                                  (value & (mask & 0xFFFF));

    return 0;
}

uint32_t HAL_GPIO_ReadPort(HAL_GPIO_Port_t port) {
    if (port >= 6) return 0;

    return gpio_mock.port[port].state;
}

int HAL_GPIO_SystemInit(void) {
    memset(&gpio_mock, 0, sizeof(gpio_mock));
    gpio_mock.initialized = true;
    return 0;
}

/* ==================== Mock Access Functions (for testing) ==================== */

/**
 * @brief Get mock GPIO state (for test assertions)
 */
uint16_t HAL_GPIO_Mock_GetPortState(HAL_GPIO_Port_t port) {
    if (port >= 6) return 0;
    return gpio_mock.port[port].state;
}

/**
 * @brief Get mock GPIO mode mask (for test assertions)
 */
uint16_t HAL_GPIO_Mock_GetPortMode(HAL_GPIO_Port_t port) {
    if (port >= 6) return 0;
    return gpio_mock.port[port].mode_mask;
}

/**
 * @brief Reset mock state
 */
void HAL_GPIO_Mock_Reset(void) {
    memset(&gpio_mock, 0, sizeof(gpio_mock));
}
