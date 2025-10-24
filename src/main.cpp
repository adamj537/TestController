#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "features.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"

#include "esp_log.h"
#include "usb/usb_host.h"
#include "freertos/semphr.h"
#include "ff.h"

#define TAG "MSC_HOST"

static void msc_host_task(void *arg)
{
    usb_host_config_t host_config = {
        false,                // skip_phy_setup
        false,                // root_port_unpowered
        ESP_INTR_FLAG_LEVEL1, // intr_flags
        NULL,                 // enum_filter_cb
        { 0, 0, 0 }           // fifo_settings_custom (all zero for default config)
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    // Wait for device connection and mount
    // This is simplified from the ESP-IDF example
    FATFS fs;
    FRESULT fr;
    const char *drive = "0:";
    fr = f_mount(&fs, drive, 1);
    if (fr == FR_OK) {
        ESP_LOGI(TAG, "Mass storage device mounted");
        FIL file;
        // Open file for writing (replace with actual firmware path)
        fr = f_open(&file, "0:/firmware.bin", FA_WRITE | FA_CREATE_ALWAYS);
        if (fr == FR_OK) {
            // Write dummy data for demonstration
            const char *data = "STM32 firmware image";
            UINT bw;
            fr = f_write(&file, data, strlen(data), &bw);
            if (fr == FR_OK) {
                ESP_LOGI(TAG, "Firmware written (%u bytes)", bw);
            } else {
                ESP_LOGE(TAG, "Failed to write firmware");
            }
            f_close(&file);
        } else {
            ESP_LOGE(TAG, "Failed to open file for writing");
        }
        f_unmount(drive);
    } else {
        ESP_LOGE(TAG, "Failed to mount mass storage device");
    }

    ESP_ERROR_CHECK(usb_host_uninstall());
    vTaskDelete(NULL);
}

void register_usb_shell_commands();

extern "C" void app_main()
{
    Features features;

    // Initialize console
    esp_console_config_t console_config = ESP_CONSOLE_CONFIG_DEFAULT();
    esp_console_init(&console_config);

    // Register your shell commands here
    register_usb_shell_commands();

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    // uart_config.uart_num = 0; // Not available in esp_console_dev_uart_config_t
    uart_config.baud_rate = 115200; // Match monitor speed
    uart_config.tx_gpio_num = -1; // Use ESP-IDF default TX pin
    uart_config.rx_gpio_num = -1; // Use ESP-IDF default RX pin
    // NOTE: Use a serial monitor that supports ANSI escape sequences (idf.py monitor, minicom, screen). PlatformIO's default monitor may not work with REPL.
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.task_stack_size = 16384; // Further increase stack size for REPL task
    esp_console_repl_t *repl = NULL;
    esp_err_t err = esp_console_new_repl_uart(&uart_config, &repl_config, &repl);
    printf("esp_console_new_repl_uart returned: %d (%s)\n", err, esp_err_to_name(err));
    if (err == ESP_OK && repl != NULL) {
        esp_console_start_repl(repl);
    } else {
        printf("Failed to initialize REPL: %s\n", esp_err_to_name(err));
    }

    // Enable USB mass storage programming feature at runtime
    features.usbMassStorageProgramming = true; // Set to false to disable

    if (features.usbMassStorageProgramming) {
//        xTaskCreate(msc_host_task, "msc_host_task", 4096, NULL, 5, NULL);
    }

    while (true) {
        printf("Hello, ESP32-S3 from C++!\n");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static int cmd_usb_power(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: usb_power <on|off>\n");
        return 1;
    }
    bool enable = (strcmp(argv[1], "on") == 0);
    esp_err_t err = usb_host_lib_set_root_port_power(enable);
    if (err == ESP_OK) {
        printf("USB root port power %s\n", enable ? "enabled" : "disabled");
    } else {
        printf("Failed to set USB root port power: %s\n", esp_err_to_name(err));
    }
    return 0;
}

void register_usb_shell_commands()
{
    const esp_console_cmd_t cmd = {
        .command = "usb_power",
        .help = "Enable or disable USB root port power. Usage: usb_power <on|off>",
        .hint = NULL,
        .func = &cmd_usb_power,
        .argtable = NULL,
        .func_w_context = NULL,
        .context = NULL
    };
    esp_console_cmd_register(&cmd);
}
