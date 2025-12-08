#include "hal_interface.h"
#include "driver/i2c.h"

void hal_i2c_master_init(uint8_t i2c_num, int sda, int scl, uint32_t clk_speed) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = clk_speed,
    };
    i2c_param_config(i2c_num, &conf);
    i2c_driver_install(i2c_num, conf.mode, 0, 0, 0);
}

int hal_i2c_master_write(uint8_t i2c_num, uint8_t addr, const uint8_t* data, size_t size, int timeout_ms) {
    return i2c_master_write_to_device(i2c_num, addr, data, size, timeout_ms / portTICK_PERIOD_MS);
}

int hal_i2c_master_read(uint8_t i2c_num, uint8_t addr, uint8_t* data, size_t size, int timeout_ms) {
    return i2c_master_read_from_device(i2c_num, addr, data, size, timeout_ms / portTICK_PERIOD_MS);
}
