/* dut_detect.c — DUT presence detection via U8 DAC MUX signal injection
 *
 * Drives GPIO0 (SIG) HIGH through U8 ch3 into the #3_3V_Volt_Mon pogo net.
 * Without a DUT the signal is unloaded (~2560 mV, ADC Vref saturated).  With
 * a DUT, the board's internal impedance pulls it down (~1666 mV measured on
 * G3 Rev1 hardware).  Threshold at 2000 mV gives margin on both sides.
 *
 * Publishes dut_present as a Sparkplug B DDATA metric on state change.
 *
 * Hardware dependency: HW-011 rework — U8 /EN hardwired LOW, SIG on GPIO0.
 *                     HW-012 rework — I2C SDA/SCL swap corrected.
 */

#include "dut_detect.h"
#include "cmd_swd.h"
#include "cmd_i2c.h"
#include "tc_mqtt.h"
#include "tc_statemachine.h"
#include "tcc_pinmap.h"
#include "tie_pinmap.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "dut_det";

/* ── Detection parameters ──────────────────────────────────────────────────── */

#define DUT_DETECT_U8_CH         3     /* U8 ch3 → #3_3V_Volt_Mon */
#define DUT_DETECT_TIE_MUX_IDX  1     /* MUX1 (U11) */
#define DUT_DETECT_TIE_MUX_CH   12    /* MUX1 ch12 → #3_3V_Volt_Mon */
#define DUT_DETECT_ADC128_CH     1     /* ADC128 CH1 = MUX1 output */
#define DUT_DETECT_THRESHOLD_MV  2000  /* above = no DUT, below = DUT present
                                        * G3 Rev1: no-DUT=2560 mV, DUT=~1666 mV */
#define DUT_DETECT_INTERVAL_MS   1000  /* poll interval when idle */
#define DUT_DETECT_DEBOUNCE      3     /* consecutive readings to confirm change */

/* ADC128D818 registers (subset — duplicated from cmd_selftest.c) */
#define ADDR_ADC128D818    0x1D
#define ADC128_REG_CONFIG  0x00
#define ADC128_REG_CONV_RATE 0x07
#define ADC128_REG_ADV_CFG 0x0B
#define ADC128_REG_CH_BASE 0x20
#define ADC128_VREF_MV     3000    /* MAX6103 external reference — must match ext-VREF-en in ADV_CFG */
#define ADC128_FULL        4096

/* TIE MUX1 (U11) address GPIOs */
static const int s_mux1_gpio[4] = {
    BSP_TIE_MUX1_A0, BSP_TIE_MUX1_A1,
    BSP_TIE_MUX1_A2, BSP_TIE_MUX1_A3
};

/* ── Module state ──────────────────────────────────────────────────────────── */

static volatile bool s_running;             /* cross-task: set false by stop, read by task loop (F-08) */
static bool    s_autostart_paused;
static volatile bool s_dut_present;         /* cross-task: written by detect, read by guards */
static bool    s_first_report = true;
static bool    s_boot_delay_done;           /* skip 5s init delay on re-starts */
static int64_t s_selftest_cooldown_us;      /* suppress auto-start until this time */
static int     s_last_mv;
static volatile TaskHandle_t s_task_handle; /* cross-task: NULL'd by task on exit, polled by wait_stopped (F-08) */

/* ── ADC read helpers ──────────────────────────────────────────────────────── */

static void tie_mux1_select(int ch)
{
    for (int bit = 0; bit < 4; bit++) {
        gpio_num_t pin = (gpio_num_t)s_mux1_gpio[bit];
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_set_level(pin, (ch >> bit) & 1);
    }
}

static bool adc128_read_raw_mv(uint8_t ch, int *mv_out)
{
    uint8_t buf[2];
    if (!i2c_read_reg(ADDR_ADC128D818, ADC128_REG_CH_BASE + ch, buf, 2))
        return false;
    int count = ((buf[0] << 8) | buf[1]) >> 4;
    *mv_out = count * ADC128_VREF_MV / ADC128_FULL;
    return true;
}

/* Perform a single DUT presence measurement.
 * Returns true on successful read; *mv_out receives the raw ADC mV.
 *
 * HW-012: I2C swap errata resolved — ADC128 on same bus as INA219s.
 * No i2c_reinit() needed. */
bool dut_detect_sample(int *mv_out)
{
    if (!i2c_lock(500)) return false;

    /* 1. Inject signal: U8 ch3, SIG HIGH */
    mux_select(DUT_DETECT_U8_CH, 1);

    /* 2. Ensure ADC128 is running */
    if (!i2c_ensure_initialized()) {
        mux_release();
        i2c_unlock();
        return false;
    }
    bool ok = i2c_write_reg(ADDR_ADC128D818, ADC128_REG_ADV_CFG,   0x03) &&
              i2c_write_reg(ADDR_ADC128D818, ADC128_REG_CONV_RATE, 0x01) &&
              i2c_write_reg(ADDR_ADC128D818, ADC128_REG_CONFIG,    0x01);
    if (!ok) {
        mux_release();
        i2c_unlock();
        return false;
    }

    /* 3. Select TIE MUX1 ch12 → ADC128 CH1 */
    tie_mux1_select(DUT_DETECT_TIE_MUX_CH);
    vTaskDelay(pdMS_TO_TICKS(200));  /* ADC128 first conversion cycle */

    /* 4. Read ADC */
    ok = adc128_read_raw_mv(DUT_DETECT_ADC128_CH, mv_out);

    /* 5. Release U8 SIG */
    mux_release();
    i2c_unlock();

    return ok;
}

/* ── MQTT publish ──────────────────────────────────────────────────────────── */

static void publish_dut_presence(bool present, int mv)
{
    printf("DUT detect: %s  (%d mV)\n", present ? "PRESENT" : "ABSENT", mv);
    tc_mqtt_publish_dut_presence(present, mv);
    /* Auto-start: trigger selftest-only cycle on DUT insert (if SM is idle).
     * Config key autostart/enabled (default 1) can disable this per-fixture.
     * Cooldown prevents re-triggering immediately after a selftest-only cycle. */
    if (present && !s_autostart_paused && tc_sm_state() == TC_SM_IDLE
        && esp_timer_get_time() >= s_selftest_cooldown_us) {
        tc_sm_cmd_start_selftest_only();
    }
}

/* ── Detection task ────────────────────────────────────────────────────────── */

static void dut_detect_task(void *pvarg)
{
    (void)pvarg;
    int debounce_count = 0;
    bool pending_state = false;

    /* Wait for I2C, WiFi, and MQTT to finish initializing before first sample.
     * Only on first boot — subsequent SM-driven restarts skip this delay. */
    if (!s_boot_delay_done) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        s_boot_delay_done = true;
    }

    ESP_LOGI(TAG, "DUT detect task started (threshold=%d mV, interval=%d ms)",
             DUT_DETECT_THRESHOLD_MV, DUT_DETECT_INTERVAL_MS);

    while (s_running) {
        int mv = 0;
        /* mv==0 means ADC conversion not yet complete (register still zero after
         * re-init); treat as a failed read and retry next cycle. */
        if (!dut_detect_sample(&mv) || mv == 0) {
            vTaskDelay(pdMS_TO_TICKS(DUT_DETECT_INTERVAL_MS));
            continue;
        }

        bool detected = (mv < DUT_DETECT_THRESHOLD_MV);
        s_last_mv = mv;

        if (s_first_report) {
            /* First valid reading — publish immediately */
            s_dut_present = detected;
            s_first_report = false;
            publish_dut_presence(detected, mv);
            debounce_count = 0;
        } else if (detected != s_dut_present) {
            /* State change candidate — debounce */
            if (detected == pending_state) {
                debounce_count++;
            } else {
                pending_state = detected;
                debounce_count = 1;
            }
            if (debounce_count >= DUT_DETECT_DEBOUNCE) {
                s_dut_present = detected;
                publish_dut_presence(detected, mv);
                debounce_count = 0;
            }
        } else {
            debounce_count = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(DUT_DETECT_INTERVAL_MS));
    }

    mux_release();
    ESP_LOGI(TAG, "DUT detect task stopped");
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

/* ── Public API ────────────────────────────────────────────────────────────── */

void dut_detect_start(void)
{
    if (s_running) return;
    s_running = true;
    /* s_first_report stays true from static init for the boot-time first sample.
     * SM-driven restarts (on_idle) leave it false so the DUT-present state
     * is carried over and doesn't re-trigger selftest-only. */
    TaskHandle_t handle = NULL;
    xTaskCreate(dut_detect_task, "dut_detect", 8192, NULL, 4, &handle);
    s_task_handle = handle;
}

void dut_detect_stop(void)
{
    s_running = false;
    /* Task will self-delete on next loop iteration */
}

void dut_detect_wait_stopped(void)
{
    /* Spin until task has fully exited and released the mux.
     * Worst case: 1000ms poll interval + 200ms ADC sample + margin. */
    int retries = 40;  /* 40 × 50ms = 2s max */
    while (s_task_handle != NULL && retries-- > 0)
        vTaskDelay(pdMS_TO_TICKS(50));
}

void dut_detect_pause(void)
{
    s_autostart_paused = true;
    ESP_LOGI(TAG, "DUT detect: auto-start paused");
}

void dut_detect_resume(void)
{
    s_autostart_paused = false;
    ESP_LOGI(TAG, "DUT detect: auto-start resumed");
}

/* SM bridge — called by tc_statemachine.c via extern when SM enters/leaves IDLE */
void tc_sm_dut_detect_on_idle(void)  {
    /* 10s cooldown after selftest-only to avoid re-triggering immediately */
    s_selftest_cooldown_us = esp_timer_get_time() + 10 * 1000000LL;
    dut_detect_start();
}
void tc_sm_dut_detect_off_idle(void) { dut_detect_stop(); }

bool dut_detect_present(void)
{
    return s_dut_present;
}

int dut_detect_last_mv(void)
{
    return s_last_mv;
}

/* ── Console command ──────────────────────────────────────────────────────── */

#include "esp_console.h"

static int do_dut(int argc, char **argv)
{
    if (argc < 2) goto usage;

    if (strcmp(argv[1], "start") == 0) {
        dut_detect_start();
        printf("DUT detect: started\n");
        return 0;
    }
    if (strcmp(argv[1], "stop") == 0) {
        dut_detect_stop();
        printf("DUT detect: stopped\n");
        return 0;
    }
    if (strcmp(argv[1], "status") == 0) {
        printf("DUT detect: %s  present=%s  autostart=%s  last=%d mV\n",
               s_running ? "running" : "stopped",
               s_dut_present ? "true" : "false",
               s_autostart_paused ? "paused" : "enabled",
               s_last_mv);
        return 0;
    }
    if (strcmp(argv[1], "pause") == 0) {
        dut_detect_pause();
        printf("DUT detect: auto-start paused\n");
        return 0;
    }
    if (strcmp(argv[1], "resume") == 0) {
        dut_detect_resume();
        printf("DUT detect: auto-start resumed\n");
        return 0;
    }
    if (strcmp(argv[1], "sample") == 0) {
        int mv = 0;
        bool ok = dut_detect_sample(&mv);
        if (ok) {
            bool det = (mv < DUT_DETECT_THRESHOLD_MV);
            printf("DUT sample: %d mV  → %s  (threshold %d mV)\n",
                   mv, det ? "PRESENT" : "ABSENT", DUT_DETECT_THRESHOLD_MV);
        } else {
            printf("DUT sample: FAILED (I2C/ADC error)\n");
        }
        return 0;
    }

usage:
    printf("Usage:\n"
           "  dut start    start background detection task\n"
           "  dut stop     stop detection task\n"
           "  dut pause    suppress auto-start (keep detection running)\n"
           "  dut resume   re-enable auto-start\n"
           "  dut status   show current state\n"
           "  dut sample   take one reading now\n");
    return 1;
}

void register_dut_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "dut",
        .help    = "DUT presence detection: dut <start|stop|status|sample>",
        .hint    = NULL,
        .func    = &do_dut,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
