#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── TC Local HMI — Feature 5 ─────────────────────────────────────────────── *
 *
 * Manages the physical HMI: LCD1602 display, start/abort buttons, and
 * button LED rings.  Communicates with the state machine via plain MQTT:
 *
 *   fixture/{serial}/hmi/button  ← published by this module on button press
 *   fixture/{serial}/hmi/status  → subscribed by this module; drives display + LEDs
 *
 * Call order:
 *   1. tc_hmi_init()   — once at startup, before tc_mqtt_start()
 *   2. tc_hmi_start()  — after MQTT is running; starts the FreeRTOS HMI task
 *
 * The state machine does NOT call tc_hmi_* directly.  The SM publishes
 * hmi/status via tc_mqtt; this module subscribes and drives hardware.
 *
 * GPIO assignments are TBD — see HMI_BTN_* and HMI_LED_* defines below.
 * When GPIO is confirmed from TCC schematic, remove the #if guards and
 * set the correct pin numbers.
 */

/* ── GPIO pin assignments (TBD — confirm from TCC schematic) ─────────────── *
 *
 * LED rings are 12V and require a transistor/MOSFET driver on TCC.
 * These GPIOs switch the driver gate/base, not the LED directly.
 *
 * Set to the actual GPIO numbers once confirmed.  Until then,
 * HMI_GPIO_ASSIGNED = 0 stubs out all GPIO operations safely.       */

#define HMI_GPIO_ASSIGNED    0       /* TODO: set to 1 when pins are confirmed */

#define HMI_BTN_GREEN_GPIO   (-1)    /* TODO: green momentary button input     */
#define HMI_BTN_RED_GPIO     (-1)    /* TODO: red momentary button input       */
#define HMI_LED_GREEN_GPIO   (-1)    /* TODO: green LED ring driver output     */
#define HMI_LED_RED_GPIO     (-1)    /* TODO: red LED ring driver output       */

/* Active levels — update if hardware is active-low */
#define HMI_BTN_ACTIVE_LEVEL  0      /* buttons pull to GND when pressed       */
#define HMI_LED_ON_LEVEL      1      /* LED on = driver gate high              */

/* ── LCD1602 via PCF8574 I2C backpack ───────────────────────────────────────
 * Uses the shared TCC I2C bus (SDA=GPIO15, SCL=GPIO16, I2C_NUM_0).
 * Standard PCF8574 backpack address.  Some boards ship at 0x3F — check
 * label on backpack module if LCD does not respond.                   */
#define HMI_LCD_I2C_ADDR     0x27    /* PCF8574 default; alt: 0x3F            */

/* ── Public API ──────────────────────────────────────────────────────────── */

/* Initialize LCD and GPIO.  Must be called before tc_mqtt_start().
 * LCD I2C absence is non-fatal — logged in DDATA, continues to IDLE. */
void tc_hmi_init(void);

/* Start the HMI FreeRTOS task.  Call after MQTT is running. */
void tc_hmi_start(void);

/* Run the startup button self-test (prompts operator to press each button).
 * Blocks for up to 10 s; times out gracefully.  Called from hmi_task. */
void tc_hmi_selftest(void);

/* Called by tc_mqtt when an hmi/status message arrives.
 * Updates display lines and LED states.  Safe to call from MQTT task. */
void tc_hmi_on_status(const char *line1, const char *line2,
                      bool led_green, bool led_red,
                      bool led_green_blink, bool led_red_blink);

#ifdef __cplusplus
}
#endif
