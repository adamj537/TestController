#include <stdio.h>
#include "nvs_flash.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#ifdef CONFIG_SPIRAM
#include "esp_psram.h"
#include "esp_private/esp_psram_extram.h"
#endif
#include "version.h"

extern "C" {
#include "cmd_gpio.h"
#include "cmd_pwm.h"
#include "cmd_adc.h"
#include "cmd_i2c.h"
#include "cmd_wifi.h"
#include "cmd_ota.h"
#include "cmd_selftest.h"
#include "cmd_vdac.h"
#include "cmd_uart.h"
#include "cmd_swd.h"
#include "cmd_mqtt.h"
#include "cmd_statemachine.h"
#include "tc_mqtt.h"
#include "tc_statemachine.h"
#include "tc_hmi.h"
#include "net_console.h"
}

static const char *TAG = "g3-tc";

static void initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

/* Confirm OTA image is valid to prevent rollback after successful boot */
static void ota_rollback_guard(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t   state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "OTA: partition '%s' marked valid", running->label);
    }
}

extern "C" void app_main(void)
{
#ifdef CONFIG_SPIRAM
    {
        esp_err_t psram_err = esp_psram_init();
        if (psram_err != ESP_OK) {
            ESP_LOGE(TAG, "esp_psram_init failed: %s", esp_err_to_name(psram_err));
        } else {
            /* Register PSRAM with the heap caps allocator (not done automatically
             * unless CONFIG_SPIRAM_BOOT_HW_INIT is set, which requires bootloader
             * PSRAM init and causes WDT crashes on this hardware configuration). */
            esp_err_t heap_err = esp_psram_extram_add_to_heap_allocator();
            if (heap_err != ESP_OK) {
                ESP_LOGE(TAG, "PSRAM heap reg failed: %s", esp_err_to_name(heap_err));
            } else {
                ESP_LOGI(TAG, "PSRAM heap OK, total=%uKB free_spiram=%uKB",
                         (unsigned)(esp_psram_get_size() / 1024),
                         (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
            }
        }
    }
#endif

    initialize_nvs();
    ota_rollback_guard();
    tc_sm_init();
    tc_hmi_init();    /* GPIO + LCD setup before MQTT/WiFi tasks start */
    tc_mqtt_init();

    ESP_LOGI(TAG, "G3 TC bringup shell  fw=%s", FW_VERSION_FULL);

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();

    /* Version in prompt — visible after every command response */
    static char prompt_buf[40];
    snprintf(prompt_buf, sizeof(prompt_buf), "g3-tc|" FW_VERSION_STRING ">");
    repl_cfg.prompt = prompt_buf;
    repl_cfg.max_cmdline_length = 256;

    esp_console_register_help_command();
    register_gpio_commands();
    register_pwm_commands();
    register_adc_commands();
    register_i2c_commands();
    register_wifi_commands();
    register_ota_commands();
    register_selftest_commands();
    register_vdac_commands();
    register_uart_commands();
    register_swd_commands();
    register_mqtt_commands();
    register_statemachine_commands();

    /* WiFi init — sets up netif/event loop and auto-connects if NVS creds exist.
     * Must happen before net_console_start() which needs the TCP/IP stack. */
    wifi_init();

    /* MQTT client — deferred until WiFi has an IP (registers GOT_IP handler).
     * No-op if broker URL not yet configured in NVS. */
    tc_mqtt_start();

    /* HMI task — starts after MQTT is running so button publishes can be delivered */
    tc_hmi_start();

    /* Configure INA219s at boot so current readings are valid immediately
     * without requiring a selftest run first. */
    selftest_ina219_init();

    /* TCP console server — listens on port 4242, accepts when WiFi is up.
     * All stdout/stderr is tee'd to the connected client via __wrap__write_r. */
    net_console_start(4242);

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hw_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_cfg, &repl_cfg, &repl));
#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_console_dev_usb_cdc_config_t hw_cfg = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&hw_cfg, &repl_cfg, &repl));
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t hw_cfg = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_cfg, &repl_cfg, &repl));
#else
#error Unsupported console type
#endif

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
