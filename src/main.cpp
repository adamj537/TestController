#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "usb/usb_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "example";
#define PROMPT_STR CONFIG_IDF_TARGET

// USB power shell command handler
// USB host stack state
static bool usb_host_enabled = false;

static int usb_power_cmd(int argc, char **argv)
{
    if (argc != 2) {
        ESP_LOGI(TAG, "Usage: usb_power <on|off>");
        return 1;
    }
    if (strcmp(argv[1], "on") == 0) {
        if (!usb_host_enabled) {
            ESP_LOGI(TAG, "Enabling USB host...");
            usb_host_config_t host_config = {
                .skip_phy_setup = false,
                .root_port_unpowered = false,
                .intr_flags = ESP_INTR_FLAG_LEVEL1,
                .enum_filter_cb = NULL,
                .fifo_settings_custom = {0, 0, 0},
            };
            esp_err_t err = usb_host_install(&host_config);
            if (err == ESP_OK) {
                usb_host_enabled = true;
                ESP_LOGI(TAG, "USB host enabled");
            } else {
                ESP_LOGE(TAG, "Failed to enable USB host: %s", esp_err_to_name(err));
                return 1;
            }
        } else {
            ESP_LOGI(TAG, "USB host already enabled");
        }
    } else if (strcmp(argv[1], "off") == 0) {
        if (usb_host_enabled) {
            ESP_LOGI(TAG, "Disabling USB host...");
            esp_err_t err = usb_host_uninstall();
            if (err == ESP_OK) {
                usb_host_enabled = false;
                ESP_LOGI(TAG, "USB host disabled");
            } else {
                ESP_LOGE(TAG, "Failed to disable USB host: %s", esp_err_to_name(err));
                return 1;
            }
        } else {
            ESP_LOGI(TAG, "USB host already disabled");
        }
    } else {
        ESP_LOGI(TAG, "Invalid argument: %s", argv[1]);
        return 1;
    }
    return 0;
}

static const esp_console_cmd_t usb_power = {
    .command = "usb_power",
    .help = "Control USB power: usb_power <on|off>",
    .hint = NULL,
    .func = &usb_power_cmd,
    .argtable = NULL,
    .func_w_context = NULL,
    .context = NULL
};
/* Basic console example (esp_console_repl API)

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "nvs_flash.h"
// #include "cmd_system.h"
// #include "cmd_wifi.h"
// #include "cmd_nvs.h"

/*
 * We warn if a secondary serial console is enabled. A secondary serial console is always output-only and
 * hence not very useful for interactive console applications. If you encounter this warning, consider disabling
 * the secondary serial console in menuconfig unless you know what you are doing.
 */
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#if !CONFIG_ESP_CONSOLE_SECONDARY_NONE
#warning "A secondary serial console is not useful when using the console component. Please disable it in menuconfig."
#endif
#endif

#include "esp_console.h"
#define PROMPT_STR CONFIG_IDF_TARGET

/* Console command history can be stored to and loaded from a file.
 * The easiest way to do this is to use FATFS filesystem on top of
 * wear_levelling library.
 */
#if CONFIG_CONSOLE_STORE_HISTORY

#define MOUNT_PATH "/data"
#define HISTORY_PATH MOUNT_PATH "/history.txt"

static void initialize_filesystem(void)
{
    static wl_handle_t wl_handle;
    const esp_vfs_fat_mount_config_t mount_config = {
            .max_files = 4,
            .format_if_mount_failed = true
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(MOUNT_PATH, "storage", &mount_config, &wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        return;
    }
}
#endif // CONFIG_STORE_HISTORY

static void initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

extern "C" void app_main(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    repl_config.prompt = PROMPT_STR ">";
    repl_config.max_cmdline_length = 256;

    initialize_nvs();

#if CONFIG_CONSOLE_STORE_HISTORY
    initialize_filesystem();
    repl_config.history_save_path = HISTORY_PATH;
    ESP_LOGI(TAG, "Command history enabled");
#else
    ESP_LOGI(TAG, "Command history disabled");
#endif


    /* Register commands */
    esp_console_register_help_command();
    ESP_ERROR_CHECK(esp_console_cmd_register(&usb_power));

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));

#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_console_dev_usb_cdc_config_t hw_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&hw_config, &repl_config, &repl));

#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));

#else
#error Unsupported console type
#endif

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}