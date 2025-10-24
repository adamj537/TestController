#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"

extern "C" void app_main(void)
{
    // Initialize console
    esp_console_config_t console_config = ESP_CONSOLE_CONFIG_DEFAULT();
    esp_console_init(&console_config);

    // UART config for REPL
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    uart_config.baud_rate = 115200;
    uart_config.tx_gpio_num = -1; // Use default TX pin
    uart_config.rx_gpio_num = -1; // Use default RX pin

    // REPL config
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "esp> ";

    esp_console_repl_t *repl = NULL;
    esp_err_t err = esp_console_new_repl_uart(&uart_config, &repl_config, &repl);
    if (err == ESP_OK && repl != NULL) {
        esp_console_start_repl(repl);
    }

    // Main loop
    while (true) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}