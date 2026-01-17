/**
 * @file hal_gpio_esp32.c
 * @brief ESP32-specific GPIO HAL implementation
 *
 * Uses ESP-IDF GPIO API to implement platform-independent GPIO interface.
 * ESP32 has 34 GPIO pins with flexible I/O capabilities.
 */

#include "../hal_gpio.h"
#include "driver/gpio.h"
#include <string.h>

/* ==================== GPIO Validation ==================== */

static bool is_valid_port(HAL_GPIO_Port_t port) {
    /* ESP32 doesn't have traditional ports, map to GPIO number ranges */
    return port >= HAL_GPIO_PORT_A && port <= HAL_GPIO_PORT_F;
}

static bool is_valid_pin(uint8_t pin) {
    return pin <= 31;  /* ESP32 has pins 0-31 (GPIO0-GPIO31) */
}

static gpio_num_t port_pin_to_gpio(HAL_GPIO_Port_t port, uint8_t pin) {
    /* Simple mapping: PORT=0 → GPIO0-15, PORT=1 → GPIO16-31, etc. */
    uint8_t gpio_num = (port * 16) + pin;
    if (gpio_num > 31) {
        return GPIO_NUM_MAX;
    }
    return (gpio_num_t)gpio_num;
}

static gpio_mode_t hal_mode_to_esp_mode(HAL_GPIO_Mode_t mode) {
    switch (mode) {
        case HAL_GPIO_MODE_INPUT:
            return GPIO_MODE_INPUT;
        case HAL_GPIO_MODE_OUTPUT:
            return GPIO_MODE_OUTPUT;
        case HAL_GPIO_MODE_INPUT_PULLUP:
            return GPIO_MODE_INPUT;  /* Pull-up handled separately */
        case HAL_GPIO_MODE_INPUT_PULLDOWN:
            return GPIO_MODE_INPUT;  /* Pull-down handled separately */
        case HAL_GPIO_MODE_ANALOG:
            return GPIO_MODE_INPUT;  /* GPIO doesn't do analog (use ADC) */
        case HAL_GPIO_MODE_OPEN_DRAIN:
            return GPIO_MODE_OUTPUT_OD;
        default:
            return GPIO_MODE_DISABLE;
    }
}

/* ==================== State Tracking ==================== */

static struct {
    bool initialized;
    uint32_t pin_modes[2];  /* Track which pins are configured */
} gpio_state = {false, {0, 0}};

/* ==================== Initialization ==================== */

int HAL_GPIO_SystemInit(void) {
    if (gpio_state.initialized) return 0;

    /* ESP32 GPIO subsystem is initialized by default via esp_idf */
    /* Additional configuration can be done here if needed */

    gpio_state.initialized = true;
    return 0;
}

/* ==================== GPIO Operations ==================== */

int HAL_GPIO_Init(HAL_GPIO_Port_t port, uint8_t pin, HAL_GPIO_Mode_t mode) {
    if (!gpio_state.initialized) return -1;

    if (!is_valid_port(port) || !is_valid_pin(pin)) {
        return -1;
    }

    gpio_num_t gpio_num = port_pin_to_gpio(port, pin);
    if (gpio_num == GPIO_NUM_MAX) {
        return -1;
    }

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = hal_mode_to_esp_mode(mode),
        .pin_bit_mask = (1ULL << gpio_num),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };

    /* Configure pull-up/pull-down if requested */
    if (mode == HAL_GPIO_MODE_INPUT_PULLUP) {
        io_conf.pull_up_en = 1;
    } else if (mode == HAL_GPIO_MODE_INPUT_PULLDOWN) {
        io_conf.pull_down_en = 1;
    }

    if (gpio_config(&io_conf) != ESP_OK) {
        return -1;
    }

    /* Mark pin as initialized */
    uint32_t bit = (port * 16) + pin;
    if (bit < 32) {
        gpio_state.pin_modes[0] |= (1 << bit);
    } else {
        gpio_state.pin_modes[1] |= (1 << (bit - 32));
    }

    return 0;
}

int HAL_GPIO_WritePin(HAL_GPIO_Port_t port, uint8_t pin, HAL_GPIO_PinState_t state) {
    if (!gpio_state.initialized) return -1;

    if (!is_valid_port(port) || !is_valid_pin(pin)) {
        return -1;
    }

    gpio_num_t gpio_num = port_pin_to_gpio(port, pin);
    if (gpio_num == GPIO_NUM_MAX) {
        return -1;
    }

    gpio_set_level(gpio_num, state == HAL_GPIO_PIN_SET ? 1 : 0);
    return 0;
}

int HAL_GPIO_ReadPin(HAL_GPIO_Port_t port, uint8_t pin) {
    if (!gpio_state.initialized) return -1;

    if (!is_valid_port(port) || !is_valid_pin(pin)) {
        return -1;
    }

    gpio_num_t gpio_num = port_pin_to_gpio(port, pin);
    if (gpio_num == GPIO_NUM_MAX) {
        return -1;
    }

    return gpio_get_level(gpio_num);
}

int HAL_GPIO_TogglePin(HAL_GPIO_Port_t port, uint8_t pin) {
    if (!gpio_state.initialized) return -1;

    if (!is_valid_port(port) || !is_valid_pin(pin)) {
        return -1;
    }

    gpio_num_t gpio_num = port_pin_to_gpio(port, pin);
    if (gpio_num == GPIO_NUM_MAX) {
        return -1;
    }

    int current = gpio_get_level(gpio_num);
    gpio_set_level(gpio_num, current ? 0 : 1);

    return 0;
}

int HAL_GPIO_WritePortMasked(HAL_GPIO_Port_t port, uint32_t mask, uint32_t value) {
    if (!gpio_state.initialized) return -1;

    if (!is_valid_port(port)) {
        return -1;
    }

    /* Write individual pins for masked bits */
    for (uint8_t pin = 0; pin < 16; pin++) {
        if (mask & (1 << pin)) {
            uint8_t pin_value = (value & (1 << pin)) ? 1 : 0;
            HAL_GPIO_WritePin(port, pin, pin_value);
        }
    }

    return 0;
}

uint32_t HAL_GPIO_ReadPort(HAL_GPIO_Port_t port) {
    if (!gpio_state.initialized) {
        return 0;
    }

    if (!is_valid_port(port)) {
        return 0;
    }

    uint32_t port_value = 0;

    /* Read all pins in the port */
    for (uint8_t pin = 0; pin < 16; pin++) {
        gpio_num_t gpio_num = port_pin_to_gpio(port, pin);
        if (gpio_num == GPIO_NUM_MAX) {
            continue;
        }

        if (gpio_get_level(gpio_num)) {
            port_value |= (1 << pin);
        }
    }

    return port_value;
}

/* ==================== ESP32-Specific ==================== */

/**
 * @brief Get GPIO number from port/pin
 *
 * Useful for direct GPIO API access if needed.
 *
 * @param port GPIO port
 * @param pin Pin number
 *
 * @return ESP32 GPIO number, or GPIO_NUM_MAX if invalid
 */
gpio_num_t HAL_GPIO_ESP32_GetGpioNum(HAL_GPIO_Port_t port, uint8_t pin) {
    if (!is_valid_port(port) || !is_valid_pin(pin)) {
        return GPIO_NUM_MAX;
    }

    return port_pin_to_gpio(port, pin);
}

/**
 * @brief Configure GPIO interrupt (ESP32-specific)
 *
 * Enables GPIO interrupt detection.
 *
 * @param port GPIO port
 * @param pin Pin number
 * @param intr_type Interrupt type (falling/rising/both/low/high)
 *
 * @return 0 on success, -1 on error
 */
int HAL_GPIO_ESP32_SetInterrupt(HAL_GPIO_Port_t port, uint8_t pin,
                                gpio_int_type_t intr_type) {
    if (!gpio_state.initialized) return -1;

    gpio_num_t gpio_num = port_pin_to_gpio(port, pin);
    if (gpio_num == GPIO_NUM_MAX) {
        return -1;
    }

    if (gpio_set_intr_type(gpio_num, intr_type) != ESP_OK) {
        return -1;
    }

    return 0;
}
