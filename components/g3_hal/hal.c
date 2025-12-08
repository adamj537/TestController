#include "hal_interface.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_log.h"

static const char* TAG = "HAL";

void hal_init(void) {
    ESP_LOGI(TAG, "HAL Initialized");
}
