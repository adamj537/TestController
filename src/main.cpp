#include <stdio.h>
#include "nvs_flash.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_wifi.h"
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
#include "dut_detect.h"
#include "recipe_json.h"
#include "tc_config.h"
#include "tc_cal.h"
#include "../storage/storage.h"
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

/* ── OTA health check — deferred validation with auto-rollback ────────────── *
 * After OTA, the new partition boots in ESP_OTA_IMG_PENDING_VERIFY state.    *
 * We defer marking it valid until basic health checks pass:                  *
 *   1. I2C bus + INA219s + ADC128 respond                                    *
 *   2. WiFi connects (IP obtained)                                           *
 *   3. MQTT broker connects                                                  *
 * If all pass within OTA_HEALTH_TIMEOUT_S, the partition is marked valid.    *
 * If timeout expires, the partition is marked invalid and the ESP32 reboots  *
 * into the previous known-good partition.                                    */

#define OTA_HEALTH_TIMEOUT_S  30

static void ota_health_check_task(void *arg)
{
    (void)arg;
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;

    /* Only run health check if we're in pending-verify state (post-OTA) */
    if (esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "OTA: partition '%s' already validated (state=%d)", running->label, (int)state);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGW(TAG, "OTA: partition '%s' PENDING VERIFY — health check starting (%ds timeout)",
             running->label, OTA_HEALTH_TIMEOUT_S);
    printf("OTA health check: %ds to validate or rollback\n", OTA_HEALTH_TIMEOUT_S);

    int64_t deadline = esp_timer_get_time() + (int64_t)OTA_HEALTH_TIMEOUT_S * 1000000LL;
    bool i2c_ok = false;
    bool wifi_ok = false;
    bool mqtt_ok = false;

    while (esp_timer_get_time() < deadline) {
        /* Check I2C — probe INA219 #0 */
        if (!i2c_ok) {
            if (i2c_ensure_initialized() && i2c_probe(0x40)) {
                i2c_ok = true;
                ESP_LOGI(TAG, "OTA health: I2C OK");
            }
        }

        /* Check WiFi — has IP? */
        if (!wifi_ok) {
            wifi_ap_record_t ap = {};
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                wifi_ok = true;
                ESP_LOGI(TAG, "OTA health: WiFi OK (RSSI=%d)", ap.rssi);
            }
        }

        /* Check MQTT */
        if (!mqtt_ok) {
            if (tc_mqtt_connected()) {
                mqtt_ok = true;
                ESP_LOGI(TAG, "OTA health: MQTT OK");
            }
        }

        if (i2c_ok && wifi_ok && mqtt_ok) break;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (i2c_ok && wifi_ok && mqtt_ok) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "OTA health: ALL PASSED — partition '%s' marked VALID", running->label);
        printf("OTA health: PASSED — firmware validated\n");
    } else {
        ESP_LOGE(TAG, "OTA health: FAILED (i2c=%d wifi=%d mqtt=%d) — ROLLING BACK",
                 i2c_ok, wifi_ok, mqtt_ok);
        printf("OTA health: FAILED — rolling back to previous firmware!\n");
        vTaskDelay(pdMS_TO_TICKS(1000));  /* let message flush */
        esp_ota_mark_app_invalid_rollback_and_reboot();
        /* does not return */
    }

    vTaskDelete(NULL);
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
    /* OTA rollback guard is deferred — see ota_health_check_task below */
    Storage_Init();   /* SPIFFS recipe partition — formats on first boot */
    tc_config_load(); /* Device config: operational params (limits, fixture ID) */
    tc_cal_load();    /* Calibration: VDUT slope/intercept, INA219, ADC128 */
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
    repl_cfg.max_cmdline_length = 4096;  /* large enough for base64-encoded recipe JSON upload */

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
    register_dut_commands();
    register_recipe_commands();
    register_config_commands();
    register_cal_commands();

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

    /* OTA health check — runs in background, validates I2C + WiFi + MQTT.
     * If all pass within 30s, marks partition valid.
     * If timeout, rolls back to previous partition automatically. */
    xTaskCreate(ota_health_check_task, "ota_health", 4096, NULL, 3, NULL);

    /* DUT presence detection — disabled at auto-start until I2C bus gets
     * a mutex (i2c_reinit is not thread-safe with HMI/INA219 tasks).
     * Start manually via: dut detect start */
    /* dut_detect_start(); */

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
