#ifndef HAL_INTERFACE_H
#define HAL_INTERFACE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void hal_init(void);

// GPIO
void hal_gpio_set_direction(uint8_t pin, uint8_t direction);
void hal_gpio_set_level(uint8_t pin, uint8_t level);
uint8_t hal_gpio_get_level(uint8_t pin);

// I2C
void hal_i2c_master_init(uint8_t i2c_num, int sda, int scl, uint32_t clk_speed);
int hal_i2c_master_write(uint8_t i2c_num, uint8_t addr, const uint8_t* data, size_t size, int timeout_ms);
int hal_i2c_master_read(uint8_t i2c_num, uint8_t addr, uint8_t* data, size_t size, int timeout_ms);

// SPI
void hal_spi_master_init(uint8_t spi_num, int mosi, int miso, int sclk, int cs);
int hal_spi_master_transmit(uint8_t spi_num, const uint8_t* tx_data, uint8_t* rx_data, size_t size);


#ifdef __cplusplus
}
#endif

#endif // HAL_INTERFACE_H
