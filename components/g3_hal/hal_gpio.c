#include "hal_interface.h"
#include "driver/gpio.h"

void hal_gpio_set_direction(uint8_t pin, uint8_t direction) {
    gpio_set_direction(pin, direction);
}

void hal_gpio_set_level(uint8_t pin, uint8_t level) {
    gpio_set_level(pin, level);
}

uint8_t hal_gpio_get_level(uint8_t pin) {
    return gpio_get_level(pin);
}
