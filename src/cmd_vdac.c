#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cmd_i2c.h"
#include "cmd_vdac.h"
#include "tc_cal.h"
#include "dut_detect.h"

/* ── TCC carrier v1 pin assignments ──────────────────────────────────────── */

#define VDUT1_PWM_GPIO   1   /* LEDC → RC filter → U1 (MP2315SGJ-Z) feedback */
#define VDUT2_PWM_GPIO   2   /* LEDC → RC filter → U7 (MP2315SGJ-Z) feedback */
#define VDUT1_ENA_GPIO  46   /* active-high enable for U1 */
#define VDUT2_ENA_GPIO  47   /* active-high enable for U7 */

/* ── ADC128D818 — same bus as INA219s (HW-012 swap resolved) ──────────────── */

#define ADDR_ADC128D818    0x1D
#define ADC128_REG_CONFIG    0x00   /* bit0=START, bit7=INIT(resets all regs, self-clearing) */
#define ADC128_REG_CONV_RATE 0x07   /* 0=low-power (~728ms/scan), 1=high-rate (~12ms/ch) */
#define ADC128_REG_ADV_CFG   0x0B   /* bit0=ext-VREF-en, bits[2:1]=mode: 0x03=Mode1+ext-VREF */
#define ADC128_REG_CH_BASE 0x20   /* CH0=0x20 … CH7=0x27, 2 bytes, left-justified 12-bit */
#define ADC128_VREF_MV     2560
#define ADC128_FULL        4096

/* CH6 = VDUT1Mon, CH7 = VDUT2Mon — voltage dividers not wired on TCC v1.1.
 * INA219 Vbus is the primary voltage measurement path on this board spin. */
#define ADC128_MON_NUM  4
#define ADC128_MON_DEN  1

/* ── INA219 Vbus — primary VDUT voltage measurement ─────────────────────── */
#define INA219_0_ADDR       0x40
#define INA219_1_ADDR       0x41
#define INA219_REG_BUS_V    0x02   /* Bus voltage; 12-bit left-justified, 4mV LSB */

/* ── LEDC — Timer 1 (Timer 0 is reserved by cmd_pwm) ────────────────────── */

#define VDAC_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define VDAC_LEDC_TIMER     LEDC_TIMER_1
#define VDAC_LEDC_DUTY_RES  LEDC_TIMER_12_BIT   /* 0–4095 */
#define VDAC_LEDC_FREQ_HZ   10000               /* 10 kHz carrier */
#define VDAC_CH1            LEDC_CHANNEL_0      /* GPIO1 → VDUT1 (HW-013 rework; was GPIO19) */
#define VDAC_CH2            LEDC_CHANNEL_1      /* GPIO2 → VDUT2 (HW-013 rework; was GPIO20) */

/* Regulator settling time after a duty step change.
 * RC filter τ = 1kΩ × 0.1µF = 100µs (negligible).
 * Measured from char sweep: output takes ~750ms to settle a step change
 * through the MP2315SGJ-Z control loop + 47µF output capacitor. */
#define VDAC_STEP_SETTLE_MS   1500
/* Initial settle after enabling: allow regulator to ramp from 0 to setpoint. */
#define VDAC_INIT_SETTLE_MS   2000

/* ── Internal state ──────────────────────────────────────────────────────── */

static bool s_timer_ready = false;
static bool s_enabled[2]  = {false, false};

/* ── LEDC helpers ────────────────────────────────────────────────────────── */

static bool vdac_ledc_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = VDAC_LEDC_MODE,
        .duty_resolution = VDAC_LEDC_DUTY_RES,
        .timer_num       = VDAC_LEDC_TIMER,
        .freq_hz         = VDAC_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
        .deconfigure     = false,
    };
    if (ledc_timer_config(&timer_cfg) != ESP_OK) return false;
    s_timer_ready = true;
    return true;
}

/* ledc_channel_config re-routes the IOMUX to LEDC on each call.
 * gpio_reset_pin() clears any prior ADC or analog claim on the pad (HW-014)
 * so the LEDC driver can take ownership without emitting a warning. */
bool vdac_set_duty(int ch_idx, int duty_pct)
{
    if (!s_timer_ready && !vdac_ledc_init()) return false;
    ledc_channel_t ch = (ch_idx == 0) ? VDAC_CH1 : VDAC_CH2;
    int gpio          = (ch_idx == 0) ? VDUT1_PWM_GPIO : VDUT2_PWM_GPIO;
    gpio_reset_pin((gpio_num_t)gpio);
    uint32_t duty     = (uint32_t)(duty_pct * 4095) / 100;
    ledc_channel_config_t ch_cfg = {
        .gpio_num   = gpio,
        .speed_mode = VDAC_LEDC_MODE,
        .channel    = ch,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = VDAC_LEDC_TIMER,
        .duty       = duty,
        .hpoint     = 0,
        .flags      = {.output_invert = 0},
    };
    return ledc_channel_config(&ch_cfg) == ESP_OK;
}

bool vdac_set_enable(int ch_idx, bool enable)
{
    int gpio = (ch_idx == 0) ? VDUT1_ENA_GPIO : VDUT2_ENA_GPIO;
    if (gpio_set_direction((gpio_num_t)gpio, GPIO_MODE_OUTPUT) != ESP_OK) return false;
    gpio_set_level((gpio_num_t)gpio, enable ? 1 : 0);
    s_enabled[ch_idx] = enable;
    return true;
}

bool vdac_set_voltage(int ch_idx, int voltage_mv)
{
    int duty = tc_cal_vdut_duty_for_mv(ch_idx, voltage_mv);
    if (duty < 0) return false;   /* slope == 0 — calibration not set */
    if (duty > 100) duty = 100;
    return vdac_set_duty(ch_idx, duty);
}

bool vdac_disable(int ch_idx)
{
    vdac_set_duty(ch_idx, 0);
    return vdac_set_enable(ch_idx, false);
}

/* ── ADC128 helpers ──────────────────────────────────────────────────────── */

bool adc128_ensure_running(void)
{
    return i2c_write_reg(ADDR_ADC128D818, ADC128_REG_ADV_CFG,   0x03) &&  /* Mode 1 + ext-VREF: bits[2:1]=01 → 0x02, bit0=1 → 0x03 */
           i2c_write_reg(ADDR_ADC128D818, ADC128_REG_CONV_RATE, 0x01) &&  /* high rate ~12ms/ch */
           i2c_write_reg(ADDR_ADC128D818, ADC128_REG_CONFIG,    0x01);
}

bool adc128_read_mon(uint8_t ch, int *rail_mv_out)
{
    uint8_t buf[2];
    if (!i2c_read_reg(ADDR_ADC128D818, ADC128_REG_CH_BASE + ch, buf, 2)) return false;
    int count    = ((buf[0] << 8) | buf[1]) >> 4;
    int adc_mv   = count * ADC128_VREF_MV / ADC128_FULL;
    int raw_mv   = adc_mv * ADC128_MON_NUM / ADC128_MON_DEN;
    *rail_mv_out = tc_cal_apply_adc(ch, raw_mv);
    return true;
}

/* ── INA219 Vbus helper ──────────────────────────────────────────────────── */

static bool read_ina219_vbus(uint8_t addr, int *vbus_mv_out)
{
    uint8_t buf[2];
    if (!i2c_read_reg(addr, INA219_REG_BUS_V, buf, 2)) return false;
    /* Bits[15:3] are the 13-bit bus voltage (4 mV LSB); bits[2:0] are flags. */
    int ch   = (addr == INA219_0_ADDR) ? TC_CAL_INA_CH0 : TC_CAL_INA_CH1;
    int raw  = (((int16_t)((buf[0] << 8) | buf[1])) >> 3) * 4;
    *vbus_mv_out = tc_cal_apply_ina_v(ch, raw);
    return true;
}

/* ── vdac char ───────────────────────────────────────────────────────────── */

static int do_vdac_char(int argc, char **argv)
{
    (void)argc; (void)argv;

    /* 0. SAFETY: sweep drives >10 V at low duty — must not run with DUT present */
    {
        int mv = 0;
        bool sampled = dut_detect_sample(&mv);
        printf("DUT detect: %d mV (%s)\n", mv,
               sampled ? (mv < 1500 ? "PRESENT" : "ABSENT") : "SAMPLE FAILED");
        if (sampled && mv < 1500) {
            printf("ERROR: DUT detected (%d mV) — remove DUT before running vdac char\n", mv);
            return 1;
        }
    }

    /* 1. LEDC — both channels at 0 % */
    if (!vdac_set_duty(0, 0) || !vdac_set_duty(1, 0)) {
        printf("LEDC init failed\n"); return 1;
    }

    /* 2. Enable both regulator channels */
    if (!vdac_set_enable(0, true) || !vdac_set_enable(1, true)) {
        printf("Enable GPIO config failed\n"); return 1;
    }

    /* Allow regulator to ramp from any previous state to 0%-duty setpoint */
    printf("Settling %d ms...\n", VDAC_INIT_SETTLE_MS);
    vTaskDelay(pdMS_TO_TICKS(VDAC_INIT_SETTLE_MS));

    /* 3. Sweep 0 % → 100 % in 5 % steps.
     * INA219 Vbus is the primary voltage source — ADC128 CH6/CH7 (VDUT1Mon/
     * VDUT2Mon) are not wired on TCC v1.1 and always read 0 mV. */
    printf("VDAC Characterization  (LEDC %d kHz, %d ms/step)\n",
           VDAC_LEDC_FREQ_HZ / 1000, VDAC_STEP_SETTLE_MS);
    printf("duty%%  VDUT1_mV  VDUT2_mV\n");
    printf("-----  --------  --------\n");

    /* Capture two well-separated anchor points per channel for linear fit. */
    int cal_v50[2] = {-1, -1};
    int cal_v90[2] = {-1, -1};

    for (int pct = 0; pct <= 100; pct += 5) {
        vdac_set_duty(0, pct);
        vdac_set_duty(1, pct);
        vTaskDelay(pdMS_TO_TICKS(VDAC_STEP_SETTLE_MS));

        int v1 = -1, v2 = -1;
        bool ok1 = read_ina219_vbus(INA219_0_ADDR, &v1);
        bool ok2 = read_ina219_vbus(INA219_1_ADDR, &v2);

        if (ok1 && ok2)
            printf("  %3d    %5d     %5d\n", pct, v1, v2);
        else
            printf("  %3d    %-8s  %-8s\n", pct,
                   ok1 ? "ok" : "ERR", ok2 ? "ok" : "ERR");

        /* Calibration anchor points — both channels */
        if (pct == 50) {
            if (ok1 && v1 > 0) cal_v50[0] = v1;
            if (ok2 && v2 > 0) cal_v50[1] = v2;
        }
        if (pct == 90) {
            if (ok1 && v1 > 0) cal_v90[0] = v1;
            if (ok2 && v2 > 0) cal_v90[1] = v2;
        }
    }

    /* 4. Clean up */
    vdac_set_duty(0, 0);
    vdac_set_duty(1, 0);
    vdac_set_enable(0, false);
    vdac_set_enable(1, false);

    /* 5. Compute and persist calibration for each channel independently.
     *    slope = ΔV / Δduty  (negative — higher duty → lower voltage)
     *    intercept = V50 - slope × 50 */
    bool saved = false;
    for (int ch = 0; ch < 2; ch++) {
        if (cal_v50[ch] > 0 && cal_v90[ch] > 0 && cal_v50[ch] != cal_v90[ch]) {
            int slope     = (cal_v90[ch] - cal_v50[ch]) / (90 - 50);
            int intercept = cal_v50[ch] - slope * 50;
            printf("\nVDUT%d calibration:  slope=%d mV/%%  intercept=%d mV\n",
                   ch + 1, slope, intercept);
            printf("  Predicted V@90%%=%d mV  measured=%d mV\n",
                   slope * 90 + intercept, cal_v90[ch]);
            tc_cal_set_vdut(ch, slope, intercept);
            saved = true;
        } else {
            printf("\nVDUT%d calibration skipped — INA219 readings at 50%% or 90%% not valid\n",
                   ch + 1);
        }
    }
    if (saved) {
        if (tc_cal_save() == 0)
            printf("Calibration saved.\n");
        else
            printf("ERROR: save failed — run 'cal save' manually\n");
    }

    return 0;
}

/* ── vdac_duty — set channel duty cycle directly ─────────────────────────── */

static int do_vdac_duty(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: vdac_duty <1|2|both> <duty_pct 0-100>\n");
        return 1;
    }
    int duty = atoi(argv[2]);
    if (duty < 0 || duty > 100) { printf("duty_pct must be 0-100\n"); return 1; }

    /* DUT over-voltage protection.
     * Slope is negative: higher duty → lower voltage.
     * duty < 75% drives > ~5V which can damage a seated DUT (~4V rating). */
    if (duty < 75 && dut_detect_present()) {
        printf("ERR: duty %d%% rejected — DUT present. Minimum 75%% when DUT is seated (duty < 75%% drives > 5V).\n",
               duty);
        return 1;
    }

    bool do1 = (strcmp(argv[1], "1")    == 0 || strcmp(argv[1], "both") == 0);
    bool do2 = (strcmp(argv[1], "2")    == 0 || strcmp(argv[1], "both") == 0);
    if (!do1 && !do2) { printf("channel must be 1, 2, or both\n"); return 1; }

    if (do1) {
        /* Configure LEDC on GPIO1 BEFORE enabling the converter.
         * Enabling first causes gpio_reset_pin() inside vdac_set_duty() to
         * briefly float the MP2315SGJ-Z feedback pin, tripping protection. */
        if (!vdac_set_duty(0, duty)) {
            printf("LEDC config failed for VDUT1\n"); return 1;
        }
        if (!s_enabled[0]) {
            vdac_set_enable(0, true);
            vTaskDelay(pdMS_TO_TICKS(VDAC_INIT_SETTLE_MS));
        }
        printf("VDUT1: ENA=GPIO%d  PWM=GPIO%d  duty=%d%%\n",
               VDUT1_ENA_GPIO, VDUT1_PWM_GPIO, duty);
    }
    if (do2) {
        if (!vdac_set_duty(1, duty)) {
            printf("LEDC config failed for VDUT2\n"); return 1;
        }
        if (!s_enabled[1]) {
            vdac_set_enable(1, true);
            vTaskDelay(pdMS_TO_TICKS(VDAC_INIT_SETTLE_MS));
        }
        printf("VDUT2: ENA=GPIO%d  PWM=GPIO%d  duty=%d%%\n",
               VDUT2_ENA_GPIO, VDUT2_PWM_GPIO, duty);
    }
    return 0;
}

/* ── vdac_voltage — set channel voltage using calibration ────────────────── */

static int do_vdac_voltage(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: vdac_voltage <1|2|both> <voltage_mv>\n");
        return 1;
    }
    int target_mv = atoi(argv[2]);

    bool do1 = (strcmp(argv[1], "1")    == 0 || strcmp(argv[1], "both") == 0);
    bool do2 = (strcmp(argv[1], "2")    == 0 || strcmp(argv[1], "both") == 0);
    if (!do1 && !do2) { printf("channel must be 1, 2, or both\n"); return 1; }

    if (do1) {
        /* Same LEDC-before-enable ordering as do_vdac_duty. */
        if (!vdac_set_voltage(0, target_mv)) {
            printf("ERROR: VDUT calibration not set — run 'vdac char' first\n");
            return 1;
        }
        if (!s_enabled[0]) {
            vdac_set_enable(0, true);
            vTaskDelay(pdMS_TO_TICKS(VDAC_INIT_SETTLE_MS));
        }
        printf("VDUT1: %d mV\n", target_mv);
    }
    if (do2) {
        if (!vdac_set_voltage(1, target_mv)) {
            printf("ERROR: VDUT calibration not set — run 'vdac char' first\n");
            return 1;
        }
        if (!s_enabled[1]) {
            vdac_set_enable(1, true);
            vTaskDelay(pdMS_TO_TICKS(VDAC_INIT_SETTLE_MS));
        }
        printf("VDUT2: %d mV\n", target_mv);
    }
    return 0;
}

/* ── vdac off ────────────────────────────────────────────────────────────── */

static int do_vdac_off(int argc, char **argv)
{
    (void)argc; (void)argv;
    vdac_set_duty(0, 0);
    vdac_set_duty(1, 0);
    vdac_set_enable(0, false);
    vdac_set_enable(1, false);
    printf("VDUT1 + VDUT2 disabled\n");
    return 0;
}

/* ── Command dispatcher ──────────────────────────────────────────────────── */

static int do_vdac(int argc, char **argv)
{
    if (argc < 2) {
        printf("VDUT regulator DAC control:\n");
        printf("  vdac char                    sweep 0-100%% and print voltage table\n");
        printf("  vdac off                     disable both channels\n");
        printf("  vdac_duty <1|2|both> <pct>   enable and set duty cycle directly\n");
        printf("  vdac_voltage <1|2|both> <mV> enable and set voltage (requires cal)\n");
        return 1;
    }
    if (strcmp(argv[1], "char") == 0) return do_vdac_char(argc - 1, argv + 1);
    if (strcmp(argv[1], "off")  == 0) return do_vdac_off(argc - 1, argv + 1);
    printf("Unknown subcommand '%s'\n", argv[1]);
    return 1;
}

void register_vdac_commands(void)
{
    static const esp_console_cmd_t cmds[] = {
        {
            .command = "vdac",
            .help    = "VDUT DAC: vdac <char|off>",
            .hint    = NULL,
            .func    = &do_vdac,
        },
        {
            .command = "vdac_duty",
            .help    = "Set VDUT duty cycle: vdac_duty <1|2|both> <duty_pct 0-100>",
            .hint    = NULL,
            .func    = &do_vdac_duty,
        },
        {
            .command = "vdac_voltage",
            .help    = "Set VDUT voltage: vdac_voltage <1|2|both> <voltage_mv>  (requires calibration)",
            .hint    = NULL,
            .func    = &do_vdac_voltage,
        },
    };
    for (int i = 0; i < (int)(sizeof(cmds) / sizeof(cmds[0])); i++)
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
}
