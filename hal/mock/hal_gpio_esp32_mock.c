/**
 * @file hal_gpio_esp32_mock.c
 * @brief Mock ESP32 GPIO HAL for Linux testing
 *
 * Simulates ESP32 GPIO behavior without ESP-IDF dependencies.
 * Allows Phase 5 code to be tested on Linux without hardware.
 */

#include "../hal_gpio.h"
#include <string.h>
#include <stdio.h>

/* ==================== Mock State ==================== */

typedef struct {
    uint32_t state;        /* Bit mask of pin states */
    uint32_t direction;    /* Bit mask of pin directions (0=input, 1=output) */
    uint32_t pull_up;      /* Bit mask of pull-ups */
    uint32_t pull_down;    /* Bit mask of pull-downs */
} GPIO_Mock_t;

static GPIO_Mock_t gpio_mock[2] = {0};
static bool gpio_initialized = false;

/* Mock debug logging (can be disabled) */
#ifdef DEBUG
#define GPIO_LOG(fmt, ...) printf("[GPIO] " fmt "\n", ##__VA_ARGS__)
#else
#define GPIO_LOG(fmt, ...) do {} while(0)
#endif

/* ==================== Initialization ==================== */

int HAL_GPIO_SystemInit(void) {
    if (gpio_initialized) return 0;

    memset(&gpio_mock, 0, sizeof(gpio_mock));
    gpio_initialized = true;
    GPIO_LOG("System init");

    return 0;
}

/* ==================== GPIO Operations ==================== */

int HAL_GPIO_Init(HAL_GPIO_Port_t port, uint8_t pin, HAL_GPIO_Mode_t mode) {
    if (!gpio_initialized) return -1;
    if (port > 1 || pin > 31) return -1;

    uint32_t bit = (1UL << pin);

    /* Clear pull-ups/pull-downs */
    gpio_mock[port].pull_up &= ~bit;
    gpio_mock[port].pull_down &= ~bit;

    /* Set direction and pull configuration */
    switch (mode) {
        case HAL_GPIO_MODE_OUTPUT:
            gpio_mock[port].direction |= bit;
            GPIO_LOG("Init port %d pin %d as OUTPUT", port, pin);
            break;
        case HAL_GPIO_MODE_INPUT:
            gpio_mock[port].direction &= ~bit;
            GPIO_LOG("Init port %d pin %d as INPUT", port, pin);
            break;
        case HAL_GPIO_MODE_INPUT_PULLUP:
            gpio_mock[port].direction &= ~bit;
            gpio_mock[port].pull_up |= bit;
            GPIO_LOG("Init port %d pin %d as INPUT_PULLUP", port, pin);
            break;
        case HAL_GPIO_MODE_INPUT_PULLDOWN:
            gpio_mock[port].direction &= ~bit;
            gpio_mock[port].pull_down |= bit;
            GPIO_LOG("Init port %d pin %d as INPUT_PULLDOWN", port, pin);
            break;
        case HAL_GPIO_MODE_OPEN_DRAIN:
            gpio_mock[port].direction |= bit;
            GPIO_LOG("Init port %d pin %d as OPEN_DRAIN", port, pin);
            break;
        default:
            return -1;
    }

    return 0;
}

int HAL_GPIO_WritePin(HAL_GPIO_Port_t port, uint8_t pin, HAL_GPIO_PinState_t state) {
    if (!gpio_initialized || port > 1 || pin > 31) return -1;

    uint32_t bit = (1UL << pin);

    if (state == HAL_GPIO_PIN_SET) {
        gpio_mock[port].state |= bit;
        GPIO_LOG("Write port %d pin %d = 1", port, pin);
    } else {
        gpio_mock[port].state &= ~bit;
        GPIO_LOG("Write port %d pin %d = 0", port, pin);
    }

    return 0;
}

int HAL_GPIO_ReadPin(HAL_GPIO_Port_t port, uint8_t pin) {
    if (!gpio_initialized || port > 1 || pin > 31) return -1;

    uint32_t bit = (1UL << pin);
    int value = (gpio_mock[port].state >> pin) & 1;

    GPIO_LOG("Read port %d pin %d = %d", port, pin, value);
    return value;
}

int HAL_GPIO_TogglePin(HAL_GPIO_Port_t port, uint8_t pin) {
    if (!gpio_initialized || port > 1 || pin > 31) return -1;

    uint32_t bit = (1UL << pin);
    gpio_mock[port].state ^= bit;

    int value = (gpio_mock[port].state >> pin) & 1;
    GPIO_LOG("Toggle port %d pin %d -> %d", port, pin, value);
    return 0;
}

int HAL_GPIO_WritePortMasked(HAL_GPIO_Port_t port, uint32_t mask, uint32_t value) {
    if (!gpio_initialized || port > 1) return -1;

    gpio_mock[port].state = (gpio_mock[port].state & ~mask) | (value & mask);
    GPIO_LOG("Write port %d masked: 0x%x = 0x%x", port, mask, value & mask);
    return 0;
}

uint32_t HAL_GPIO_ReadPort(HAL_GPIO_Port_t port) {
    if (!gpio_initialized || port > 1) return 0;

    GPIO_LOG("Read port %d = 0x%x", port, gpio_mock[port].state);
    return gpio_mock[port].state;
}

/* ==================== Mock Helpers (for testing) ==================== */

/**
 * @brief Inject input value (simulate external stimulus)
 */
void HAL_GPIO_Mock_InjectInput(HAL_GPIO_Port_t port, uint8_t pin, HAL_GPIO_PinState_t state) {
    if (port > 1 || pin > 31) return;

    uint32_t bit = (1UL << pin);

    if (state == HAL_GPIO_PIN_SET) {
        gpio_mock[port].state |= bit;
    } else {
        gpio_mock[port].state &= ~bit;
    }

    GPIO_LOG("Inject port %d pin %d = %d", port, pin, state);
}

/**
 * @brief Get mock state for verification
 */
uint32_t HAL_GPIO_Mock_GetState(HAL_GPIO_Port_t port) {
    return port <= 1 ? gpio_mock[port].state : 0;
}

/**
 * @brief Reset all mock state
 */
void HAL_GPIO_Mock_Reset(void) {
    memset(&gpio_mock, 0, sizeof(gpio_mock));
    GPIO_LOG("Reset all GPIO mock state");
}

/* ==================== ESP32-Specific (stubs for compatibility) ==================== */

int HAL_GPIO_ESP32_SetInterrupt(HAL_GPIO_Port_t port, uint8_t pin, int intr_type) {
    /* Not implemented in mock, but interface exists */
    GPIO_LOG("Mock: SetInterrupt port %d pin %d type %d", port, pin, intr_type);
    return 0;
}
