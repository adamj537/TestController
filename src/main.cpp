#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "esp_log.h"
// #include "usb/usb_host.h"

#define LOG_TAG "REPL"
// static int cmd_usb_power(int argc, char **argv)
// {
//     if (argc < 2) {
//         ESP_LOGI(LOG_TAG, "Usage: usb_power <on|off>");
//         return 1;
//     }
//     bool enable = (strcmp(argv[1], "on") == 0);
//     esp_err_t err = usb_host_lib_set_root_port_power(enable);
//     if (err == ESP_OK) {
//         ESP_LOGI(LOG_TAG, "USB root port power %s", enable ? "enabled" : "disabled");
//     } else {
//         ESP_LOGE(LOG_TAG, "Failed to set USB root port power: %s", esp_err_to_name(err));
//     }
//     return 0;
// }

// static void register_usb_shell_commands(void)
// {
//     const esp_console_cmd_t cmd = {
//         .command = "usb_power",
//         .help = "Enable or disable USB root port power. Usage: usb_power <on|off>",
//         .hint = NULL,
//         .func = &cmd_usb_power,
//         .argtable = NULL,
//         .func_w_context = NULL,
//         .context = NULL
//     };
//     esp_console_cmd_register(&cmd);
// }
#include <cstdio>



extern "C" void app_main(void)
{
    // Initialize NVS (required for console/history)
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
    // Set log level to INFO for all tags
    esp_log_level_set("*", ESP_LOG_INFO);

    // Minimal REPL only, all other features commented out
    ESP_LOGI(LOG_TAG, "Starting minimal REPL...");

    esp_console_config_t console_config = ESP_CONSOLE_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_init(&console_config));
    ESP_LOGI(LOG_TAG, "called console init...");

    esp_console_register_help_command();

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_LOGI(LOG_TAG, "Trying REPL setup with defaults...");

    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "esp> ";
    repl_config.task_stack_size = 12288; // Try larger stack size
    repl_config.max_cmdline_length = 256;
    ESP_LOGI(LOG_TAG, "Trying REPL setup with stack size 12288...");

    esp_console_repl_t *repl = NULL;
    esp_err_t err = esp_console_new_repl_uart(&uart_config, &repl_config, &repl);
    ESP_LOGI(LOG_TAG, "esp_console_new_repl_uart returned: %s", esp_err_to_name(err));
    if (err != ESP_OK) {
        ESP_LOGE("REPL", "esp_console_new_repl_uart failed: %s", esp_err_to_name(err));
        return;
    }
    if (repl == NULL) {
        ESP_LOGE("REPL", "REPL pointer is NULL after creation.");
        return;
    }
    esp_err_t start_err = esp_console_start_repl(repl);
    ESP_LOGI(LOG_TAG, "esp_console_start_repl returned: %s", esp_err_to_name(start_err));
    if (start_err != ESP_OK) {
        ESP_LOGE("REPL", "esp_console_start_repl failed: %s", esp_err_to_name(start_err));
        return;
    }
    ESP_LOGI("REPL", "REPL started successfully. You should see the shell prompt.");

    // Main loop
    while (true) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}