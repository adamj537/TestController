/**
 * @file SAFE_GPIO_TEMPLATE.h
 * @brief Safe GPIO initialization patterns for ESP32 hardware protection
 *
 * This template provides safe GPIO initialization patterns that prevent
 * bricking and hardware damage during development and testing.
 *
 * @date 2026-02-15
 * @project G3 Main Board Test Fixture - Tester Client
 */

#ifndef SAFE_GPIO_TEMPLATE_H
#define SAFE_GPIO_TEMPLATE_H

#include "driver/gpio.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * ========================================================================
 * CRITICAL GPIO SAFETY RULES
 * ========================================================================
 *
 * 1. NEVER USE GPIO 6-11 (flash interface, WILL corrupt flash)
 * 2. Configure as INPUT first, read state, then OUTPUT
 * 3. Start OUTPUT pins at LOW state
 * 4. Limit per-pin current to 12mA (40mA absolute max)
 * 5. Total GPIO current <200mA (1200mA absolute max)
 * 6. Add 220Ω series resistors on outputs during testing
 * 7. Use strapping pins (0, 2, 12, 15, 45, 46) with ≥10kΩ pull resistors
 */

// GPIO current limits (mA)
#define GPIO_CURRENT_PER_PIN_RECOMMENDED    12
#define GPIO_CURRENT_PER_PIN_ABSOLUTE_MAX   40
#define GPIO_CURRENT_TOTAL_RECOMMENDED      200
#define GPIO_CURRENT_TOTAL_ABSOLUTE_MAX     1200

// Forbidden GPIOs (flash interface)
#define GPIO_FLASH_INTERFACE_MIN    6
#define GPIO_FLASH_INTERFACE_MAX    11

// Strapping pins (use with caution, ≥10kΩ pull resistors)
static const gpio_num_t STRAPPING_PINS[] = {
    GPIO_NUM_0,   // BOOT button
    GPIO_NUM_2,   // Boot mode
    GPIO_NUM_12,  // VDD_SPI voltage
    GPIO_NUM_15,  // Boot silent/verbose
    GPIO_NUM_45,  // ESP32-S3 VDD_SPI
    GPIO_NUM_46   // ESP32-S3 ROM message enable
};

/**
 * ========================================================================
 * SAFE GPIO OUTPUT INITIALIZATION
 * ========================================================================
 */

/**
 * @brief Safely initialize GPIO as output with pre-checks
 *
 * Pattern:
 *   1. Verify GPIO is not in forbidden range (6-11)
 *   2. Configure as INPUT (high-impedance, safe state)
 *   3. Read current pin state to verify external circuit
 *   4. Configure as OUTPUT
 *   5. Set initial LOW state (safe default)
 *
 * @param gpio_num GPIO pin number (ESP32-S3: 0-48 except 6-11, 22-25)
 * @return ESP_OK if successful, ESP_ERR_INVALID_ARG if unsafe GPIO
 */
static inline esp_err_t gpio_safe_init_output(gpio_num_t gpio_num)
{
    static const char *TAG = "GPIO_SAFE";

    // CRITICAL: Block GPIO 6-11 (flash interface)
    if (gpio_num >= GPIO_FLASH_INTERFACE_MIN && gpio_num <= GPIO_FLASH_INTERFACE_MAX) {
        ESP_LOGE(TAG, "BLOCKED: GPIO %d is flash interface (6-11), CANNOT BE USED!", gpio_num);
        return ESP_ERR_INVALID_ARG;
    }

    // Warn if using strapping pins
    for (int i = 0; i < sizeof(STRAPPING_PINS) / sizeof(STRAPPING_PINS[0]); i++) {
        if (gpio_num == STRAPPING_PINS[i]) {
            ESP_LOGW(TAG, "WARNING: GPIO %d is strapping pin, use ≥10kΩ pull resistor", gpio_num);
        }
    }

    ESP_LOGI(TAG, "Initializing GPIO %d as OUTPUT (safe pattern)", gpio_num);

    // 1. Configure as INPUT (high-impedance, safe)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d as INPUT", gpio_num);
        return ret;
    }

    // 2. Read current state (verify external circuit)
    int level = gpio_get_level(gpio_num);
    ESP_LOGI(TAG, "GPIO %d current state: %d (before OUTPUT mode)", gpio_num, level);

    // 3. Configure as OUTPUT
    io_conf.mode = GPIO_MODE_OUTPUT;
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d as OUTPUT", gpio_num);
        return ret;
    }

    // 4. Set initial LOW state (safe default)
    ret = gpio_set_level(gpio_num, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO %d to LOW", gpio_num);
        return ret;
    }

    ESP_LOGI(TAG, "GPIO %d initialized as OUTPUT, set to LOW (safe)", gpio_num);
    return ESP_OK;
}

/**
 * ========================================================================
 * SAFE GPIO INPUT INITIALIZATION
 * ========================================================================
 */

/**
 * @brief Safely initialize GPIO as input
 *
 * Inputs are inherently safer (high-impedance, passive), but still
 * verify GPIO is not in forbidden range.
 *
 * @param gpio_num GPIO pin number
 * @param pull_mode Pull-up/down mode (GPIO_PULLUP_ONLY, GPIO_PULLDOWN_ONLY, GPIO_FLOATING)
 * @return ESP_OK if successful, ESP_ERR_INVALID_ARG if unsafe GPIO
 */
static inline esp_err_t gpio_safe_init_input(gpio_num_t gpio_num, gpio_pull_mode_t pull_mode)
{
    static const char *TAG = "GPIO_SAFE";

    // CRITICAL: Block GPIO 6-11 (flash interface)
    if (gpio_num >= GPIO_FLASH_INTERFACE_MIN && gpio_num <= GPIO_FLASH_INTERFACE_MAX) {
        ESP_LOGE(TAG, "BLOCKED: GPIO %d is flash interface (6-11), CANNOT BE USED!", gpio_num);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing GPIO %d as INPUT (pull mode: %d)", gpio_num, pull_mode);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = (pull_mode == GPIO_PULLUP_ONLY) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (pull_mode == GPIO_PULLDOWN_ONLY) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d as INPUT", gpio_num);
        return ret;
    }

    ESP_LOGI(TAG, "GPIO %d initialized as INPUT", gpio_num);
    return ESP_OK;
}

/**
 * ========================================================================
 * SAFE ADC CONFIGURATION
 * ========================================================================
 */

/**
 * @brief Verify ADC channel is safe and available
 *
 * ESP32-S3 ADC1: GPIO 1-10 (but 6-10 are flash interface, so only 1-5 safe)
 * ESP32-S3 ADC2: GPIO 11-20 (but 11 is flash, so 12-20; avoid with WiFi)
 *
 * @param gpio_num GPIO to use for ADC
 * @return ESP_OK if safe, ESP_ERR_INVALID_ARG if unsafe
 */
static inline esp_err_t adc_verify_safe_gpio(gpio_num_t gpio_num)
{
    static const char *TAG = "ADC_SAFE";

    // ADC1 safe channels (ESP32-S3): GPIO 1-5
    if (gpio_num >= GPIO_NUM_1 && gpio_num <= GPIO_NUM_5) {
        ESP_LOGI(TAG, "ADC1 channel on GPIO %d (safe, can use with WiFi)", gpio_num);
        return ESP_OK;
    }

    // ADC2 channels (ESP32-S3): GPIO 12-20 (cannot use with WiFi)
    if (gpio_num >= GPIO_NUM_12 && gpio_num <= GPIO_NUM_20) {
        ESP_LOGW(TAG, "ADC2 channel on GPIO %d (WARNING: cannot use with WiFi)", gpio_num);
        return ESP_OK;
    }

    // Flash interface (6-11)
    if (gpio_num >= GPIO_FLASH_INTERFACE_MIN && gpio_num <= GPIO_FLASH_INTERFACE_MAX) {
        ESP_LOGE(TAG, "BLOCKED: GPIO %d is flash interface, CANNOT BE USED for ADC!", gpio_num);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGE(TAG, "GPIO %d is not a valid ADC channel on ESP32-S3", gpio_num);
    return ESP_ERR_INVALID_ARG;
}

/**
 * @brief ADC voltage safety check
 *
 * Absolute maximum input voltage: 3.6V (exceeding causes permanent damage)
 * Recommended maximum: 3.3V
 *
 * @param voltage_mv Voltage in millivolts
 * @return ESP_OK if safe, ESP_FAIL if exceeds limits
 */
static inline esp_err_t adc_verify_safe_voltage(int voltage_mv)
{
    static const char *TAG = "ADC_SAFE";

    const int ADC_ABSOLUTE_MAX_MV = 3600;  // Absolute maximum (damage above)
    const int ADC_RECOMMENDED_MAX_MV = 3300;  // Recommended maximum

    if (voltage_mv > ADC_ABSOLUTE_MAX_MV) {
        ESP_LOGE(TAG, "DANGER: ADC input %d mV exceeds absolute max %d mV (DAMAGE RISK!)",
                 voltage_mv, ADC_ABSOLUTE_MAX_MV);
        return ESP_FAIL;
    }

    if (voltage_mv > ADC_RECOMMENDED_MAX_MV) {
        ESP_LOGW(TAG, "WARNING: ADC input %d mV exceeds recommended max %d mV",
                 voltage_mv, ADC_RECOMMENDED_MAX_MV);
    }

    return ESP_OK;
}

/**
 * ========================================================================
 * USAGE EXAMPLES
 * ========================================================================
 */

#if 0  // Example code (not compiled)

void example_safe_gpio_usage(void)
{
    // Example 1: Safe OUTPUT initialization
    gpio_safe_init_output(GPIO_NUM_2);  // Initialize GPIO 2 as output
    gpio_set_level(GPIO_NUM_2, 1);      // Set HIGH after safe init

    // Example 2: Safe INPUT initialization
    gpio_safe_init_input(GPIO_NUM_4, GPIO_PULLUP_ONLY);  // Input with pull-up
    int level = gpio_get_level(GPIO_NUM_4);  // Read input state

    // Example 3: Blocked GPIO (will fail with error log)
    gpio_safe_init_output(GPIO_NUM_6);  // ERROR: Flash interface, blocked

    // Example 4: ADC safety check
    if (adc_verify_safe_gpio(GPIO_NUM_1) == ESP_OK) {
        // Safe to use GPIO 1 for ADC1
    }

    // Example 5: ADC voltage check
    int measured_voltage_mv = 3500;
    if (adc_verify_safe_voltage(measured_voltage_mv) != ESP_OK) {
        ESP_LOGE("APP", "Unsafe ADC voltage detected, aborting");
    }
}

#endif  // Example code

#ifdef __cplusplus
}
#endif

#endif  // SAFE_GPIO_TEMPLATE_H

/**
 * ========================================================================
 * INTEGRATION NOTES
 * ========================================================================
 *
 * To use in your project:
 *
 * 1. Include this header in your GPIO initialization code
 * 2. Replace direct gpio_config() calls with gpio_safe_init_output/input()
 * 3. Verify all ADC channels with adc_verify_safe_gpio()
 * 4. Check ADC voltages with adc_verify_safe_voltage() before reading
 * 5. Review ESP_LOG output for warnings and errors
 *
 * Benefits:
 * - Automatic blocking of GPIO 6-11 (prevents flash corruption)
 * - Safe initialization sequence (INPUT → verify → OUTPUT)
 * - Warning for strapping pins
 * - ADC voltage safety checks
 * - Clear logging for debugging
 *
 * This template can be adapted for your specific HAL layer or used
 * directly in application code.
 */
