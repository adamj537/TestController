#include "tc_hmi.h"
#include "tc_mqtt.h"
#include "cmd_i2c.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if HMI_GPIO_ASSIGNED
#include "driver/gpio.h"
#endif

#include <string.h>
#include <stdio.h>

static const char *TAG = "tc_hmi";

/* ── LCD1602 via PCF8574 I2C backpack ────────────────────────────────────── *
 *
 * PCF8574 bit layout (standard wiring for HD44780-compatible backpacks):
 *   P0 = RS   P1 = RW   P2 = EN   P3 = BL (backlight)
 *   P4 = D4   P5 = D5   P6 = D6   P7 = D7
 *
 * All writes are 4-bit mode.  EN pulse: set byte with EN=1, then EN=0.
 */

#define LCD_BL   (1 << 3)   /* backlight bit — keep set for backlight on */
#define LCD_EN   (1 << 2)
#define LCD_RW   (1 << 1)
#define LCD_RS   (1 << 0)

static bool s_lcd_ok = false;

/* Write one byte to the PCF8574 (8 GPIO outputs).
 * PCF8574 protocol: START | ADDR+W | DATA | STOP  (no register address byte) */
static bool lcd_pcf_write(uint8_t val)
{
    return i2c_write_raw(HMI_LCD_I2C_ADDR, &val, 1);
}

/* Pulse the EN line: write val with EN set, then EN cleared */
static void lcd_pulse_en(uint8_t val)
{
    lcd_pcf_write(val | LCD_EN);
    vTaskDelay(pdMS_TO_TICKS(1));
    lcd_pcf_write(val & ~LCD_EN);
    vTaskDelay(pdMS_TO_TICKS(1));
}

/* Send 4 high bits via PCF8574 with RS set or cleared */
static void lcd_write_nibble(uint8_t nibble, bool rs)
{
    uint8_t val = (nibble << 4) | LCD_BL | (rs ? LCD_RS : 0);
    lcd_pulse_en(val);
}

/* Send a full byte as two nibbles (high nibble first) */
static void lcd_write_byte(uint8_t byte, bool rs)
{
    lcd_write_nibble(byte >> 4, rs);
    lcd_write_nibble(byte & 0x0F, rs);
}

static void lcd_cmd(uint8_t cmd)  { lcd_write_byte(cmd,  false); }
static void lcd_data(uint8_t ch)  { lcd_write_byte(ch,   true);  }

/* Initialize LCD in 4-bit mode */
static bool lcd_init(void)
{
    if (!i2c_ensure_initialized()) {
        ESP_LOGW(TAG, "LCD: I2C bus not ready");
        return false;
    }

    /* Probe PCF8574 */
    if (!i2c_probe(HMI_LCD_I2C_ADDR)) {
        ESP_LOGW(TAG, "LCD: PCF8574 not found at 0x%02X", HMI_LCD_I2C_ADDR);
        return false;
    }

    /* HD44780 power-on init sequence (4-bit mode) */
    vTaskDelay(pdMS_TO_TICKS(50));          /* >40ms after Vcc rises to 2.7V */
    lcd_write_nibble(0x03, false);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write_nibble(0x03, false);
    vTaskDelay(pdMS_TO_TICKS(1));
    lcd_write_nibble(0x03, false);
    vTaskDelay(pdMS_TO_TICKS(1));
    lcd_write_nibble(0x02, false);          /* switch to 4-bit mode */

    lcd_cmd(0x28);   /* Function set: 4-bit, 2 lines, 5×8 dots */
    lcd_cmd(0x0C);   /* Display on, cursor off, blink off */
    lcd_cmd(0x06);   /* Entry mode: increment, no shift */
    lcd_cmd(0x01);   /* Clear display */
    vTaskDelay(pdMS_TO_TICKS(2));           /* clear takes up to 1.52ms */

    ESP_LOGI(TAG, "LCD: initialized at I2C addr 0x%02X", HMI_LCD_I2C_ADDR);
    return true;
}

/* Write up to 16 characters to a display row (0=top, 1=bottom) */
static void lcd_set_line(uint8_t row, const char *text)
{
    if (!s_lcd_ok || !text) return;

    uint8_t addr = (row == 0) ? 0x80 : 0xC0;   /* DDRAM: row 0=0x00, row 1=0x40 */
    lcd_cmd(addr);

    char padded[17] = "                ";       /* 16 spaces */
    size_t len = strlen(text);
    if (len > 16) len = 16;
    memcpy(padded, text, len);

    for (int i = 0; i < 16; i++) {
        lcd_data((uint8_t)padded[i]);
    }
}

/* ── LED state ───────────────────────────────────────────────────────────── */

typedef struct {
    bool green;
    bool red;
    bool green_blink;
    bool red_blink;
} led_state_t;

static led_state_t s_led = {0};

static void led_apply(bool green_on, bool red_on)
{
#if HMI_GPIO_ASSIGNED
    gpio_set_level(HMI_LED_GREEN_GPIO, green_on ? HMI_LED_ON_LEVEL : !HMI_LED_ON_LEVEL);
    gpio_set_level(HMI_LED_RED_GPIO,   red_on   ? HMI_LED_ON_LEVEL : !HMI_LED_ON_LEVEL);
#else
    (void)green_on; (void)red_on;
#endif
}

/* ── Button state ────────────────────────────────────────────────────────── */

static bool btn_read_green(void)
{
#if HMI_GPIO_ASSIGNED
    return gpio_get_level(HMI_BTN_GREEN_GPIO) == HMI_BTN_ACTIVE_LEVEL;
#else
    return false;
#endif
}

static bool btn_read_red(void)
{
#if HMI_GPIO_ASSIGNED
    return gpio_get_level(HMI_BTN_RED_GPIO) == HMI_BTN_ACTIVE_LEVEL;
#else
    return false;
#endif
}

/* ── HMI task ────────────────────────────────────────────────────────────── */

#define HMI_POLL_MS      50    /* button poll interval */
#define HMI_DEBOUNCE_MS 100    /* button must be held for this long */
#define HMI_BLINK_MS    500    /* LED blink half-period */

static void hmi_task(void *arg)
{
    (void)arg;

    tc_hmi_selftest();

    /* Initial display state */
    lcd_set_line(0, "READY");
    lcd_set_line(1, "");
    led_apply(true, false);   /* green solid, red off = IDLE */

    bool prev_green = false;
    bool prev_red   = false;
    uint32_t green_held_ms = 0;
    uint32_t red_held_ms   = 0;
    uint32_t blink_ms      = 0;
    bool     blink_phase   = false;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(HMI_POLL_MS));
        blink_ms += HMI_POLL_MS;

        /* ── LED blink tick ── */
        if (blink_ms >= HMI_BLINK_MS) {
            blink_ms   = 0;
            blink_phase = !blink_phase;
            bool green_on = s_led.green || (s_led.green_blink && blink_phase);
            bool red_on   = s_led.red   || (s_led.red_blink   && blink_phase);
            led_apply(green_on, red_on);
        }

        /* ── Button debounce + publish ── */
        bool green_now = btn_read_green();
        bool red_now   = btn_read_red();

        if (green_now) {
            green_held_ms += HMI_POLL_MS;
            if (green_held_ms >= HMI_DEBOUNCE_MS && !prev_green) {
                prev_green = true;
                ESP_LOGI(TAG, "button: start");
                tc_mqtt_publish_hmi_button("start");
            }
        } else {
            green_held_ms = 0;
            prev_green    = false;
        }

        if (red_now) {
            red_held_ms += HMI_POLL_MS;
            if (red_held_ms >= HMI_DEBOUNCE_MS && !prev_red) {
                prev_red = true;
                ESP_LOGI(TAG, "button: abort");
                tc_mqtt_publish_hmi_button("abort");
            }
        } else {
            red_held_ms = 0;
            prev_red    = false;
        }
    }
}

/* ── Startup selftest ────────────────────────────────────────────────────── */

#define HMI_SELFTEST_TIMEOUT_MS 10000

void tc_hmi_selftest(void)
{
    ESP_LOGI(TAG, "HMI selftest: press GREEN then RED within %d ms", HMI_SELFTEST_TIMEOUT_MS);

    lcd_set_line(0, "PRESS GREEN");
    lcd_set_line(1, "");
    led_apply(true, false);

    int64_t deadline = esp_timer_get_time() + (int64_t)HMI_SELFTEST_TIMEOUT_MS * 1000;
    bool green_ok = false;

    while (esp_timer_get_time() < deadline) {
        if (btn_read_green()) {
            green_ok = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(HMI_POLL_MS));
    }

    if (!green_ok) {
        ESP_LOGW(TAG, "HMI selftest: GREEN button timeout");
        tc_mqtt_publish_hmi_selftest("TIMEOUT");
        return;
    }

    lcd_set_line(0, "PRESS RED");
    lcd_set_line(1, "");
    led_apply(false, true);

    deadline = esp_timer_get_time() + (int64_t)HMI_SELFTEST_TIMEOUT_MS * 1000;
    bool red_ok = false;

    while (esp_timer_get_time() < deadline) {
        if (btn_read_red()) {
            red_ok = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(HMI_POLL_MS));
    }

    if (!red_ok) {
        ESP_LOGW(TAG, "HMI selftest: RED button timeout");
        tc_mqtt_publish_hmi_selftest("TIMEOUT");
        return;
    }

    ESP_LOGI(TAG, "HMI selftest: PASS");
    lcd_set_line(0, "HMI OK");
    lcd_set_line(1, "");
    tc_mqtt_publish_hmi_selftest("PASS");
    vTaskDelay(pdMS_TO_TICKS(1000));
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void tc_hmi_init(void)
{
#if HMI_GPIO_ASSIGNED
    /* Button inputs */
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << HMI_BTN_GREEN_GPIO) | (1ULL << HMI_BTN_RED_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);

    /* LED outputs */
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << HMI_LED_GREEN_GPIO) | (1ULL << HMI_LED_RED_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_cfg);
    led_apply(false, false);
#else
    ESP_LOGW(TAG, "GPIO not assigned — button/LED hardware disabled");
#endif

    s_lcd_ok = lcd_init();
    if (!s_lcd_ok) {
        ESP_LOGW(TAG, "LCD absent — display disabled; TC will continue");
        /* Logged to DDATA by tc_mqtt_publish_hmi_selftest in selftest */
    }
}

void tc_hmi_start(void)
{
    xTaskCreate(hmi_task, "hmi_task", 4096, NULL, 5, NULL);
}

void tc_hmi_on_status(const char *line1, const char *line2,
                      bool led_green, bool led_red,
                      bool led_green_blink, bool led_red_blink)
{
    /* Update display */
    lcd_set_line(0, line1 ? line1 : "");
    lcd_set_line(1, line2 ? line2 : "");

    /* Update LED state (blink driven by hmi_task tick) */
    s_led.green       = led_green       && !led_green_blink;
    s_led.red         = led_red         && !led_red_blink;
    s_led.green_blink = led_green_blink;
    s_led.red_blink   = led_red_blink;

    /* Apply steady state immediately; blink task handles toggling */
    led_apply(led_green && !led_green_blink, led_red && !led_red_blink);

    ESP_LOGI(TAG, "status: \"%s\" / \"%s\"  green=%d%s  red=%d%s",
             line1 ? line1 : "",
             line2 ? line2 : "",
             led_green, led_green_blink ? "(blink)" : "",
             led_red,   led_red_blink   ? "(blink)" : "");
}
