#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_console.h"
#include "driver/ledc.h"
#include "cmd_pwm.h"

#define PWM_MODE      LEDC_LOW_SPEED_MODE
#define PWM_TIMER     LEDC_TIMER_0
#define PWM_DUTY_RES  LEDC_TIMER_12_BIT   /* 0–4095 */
#define MAX_CHANNELS  6

static struct {
    int gpio;
    ledc_channel_t channel;
} pwm_slots[MAX_CHANNELS] = {
    {-1, LEDC_CHANNEL_0}, {-1, LEDC_CHANNEL_1}, {-1, LEDC_CHANNEL_2},
    {-1, LEDC_CHANNEL_3}, {-1, LEDC_CHANNEL_4}, {-1, LEDC_CHANNEL_5},
};

static int find_slot_by_gpio(int gpio)
{
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (pwm_slots[i].gpio == gpio) return i;
    }
    return -1;
}

static int alloc_slot(void)
{
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (pwm_slots[i].gpio < 0) return i;
    }
    return -1;
}

static int do_pwm_set(int argc, char **argv)
{
    if (argc < 4) {
        printf("Usage: pwm set <gpio> <freq_hz> <duty_pct 0-100>\n");
        return 1;
    }
    int gpio = atoi(argv[1]);
    uint32_t freq = (uint32_t)atoi(argv[2]);
    int duty_pct = atoi(argv[3]);

    if (freq == 0) { printf("freq_hz must be > 0\n"); return 1; }
    if (duty_pct < 0 || duty_pct > 100) { printf("duty_pct must be 0-100\n"); return 1; }

    /* (re)configure timer — allows frequency change between calls */
    ledc_timer_config_t timer_cfg = {
        .speed_mode = PWM_MODE,
        .duty_resolution = PWM_DUTY_RES,
        .timer_num = PWM_TIMER,
        .freq_hz = freq,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        printf("Timer config failed: %s\n", esp_err_to_name(err));
        return 1;
    }

    int slot = find_slot_by_gpio(gpio);
    if (slot < 0) {
        slot = alloc_slot();
        if (slot < 0) {
            printf("No free PWM channels (max %d active)\n", MAX_CHANNELS);
            return 1;
        }
        pwm_slots[slot].gpio = gpio;
    }

    uint32_t duty = (uint32_t)(duty_pct * 4095) / 100;

    ledc_channel_config_t ch_cfg = {
        .gpio_num = gpio,
        .speed_mode = PWM_MODE,
        .channel = pwm_slots[slot].channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = PWM_TIMER,
        .duty = duty,
        .hpoint = 0,
        .flags = {.output_invert = 0},
    };
    err = ledc_channel_config(&ch_cfg);
    if (err != ESP_OK) {
        printf("Channel config failed: %s\n", esp_err_to_name(err));
        pwm_slots[slot].gpio = -1;
        return 1;
    }

    printf("PWM GPIO%d: %u Hz  %d%%  (raw duty=%u)\n", gpio, (unsigned)freq, duty_pct, (unsigned)duty);
    return 0;
}

static int do_pwm_stop(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: pwm stop <gpio>\n");
        return 1;
    }
    int gpio = atoi(argv[1]);
    int slot = find_slot_by_gpio(gpio);
    if (slot < 0) {
        printf("GPIO%d not running PWM\n", gpio);
        return 1;
    }
    ledc_stop(PWM_MODE, pwm_slots[slot].channel, 0);
    pwm_slots[slot].gpio = -1;
    printf("PWM GPIO%d stopped\n", gpio);
    return 0;
}

static int do_pwm_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    int active = 0;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (pwm_slots[i].gpio >= 0) {
            printf("  CH%d: GPIO%d\n", i, pwm_slots[i].gpio);
            active++;
        }
    }
    if (!active) printf("  (no active PWM channels)\n");
    return 0;
}

static int do_pwm(int argc, char **argv)
{
    if (argc < 2) {
        printf("PWM (LEDC) control commands:\n");
        printf("  pwm set <gpio> <freq_hz> <duty_pct>  start/update PWM\n");
        printf("  pwm stop <gpio>                      stop PWM\n");
        printf("  pwm status                           list active channels\n");
        return 1;
    }
    if (strcmp(argv[1], "set") == 0)    return do_pwm_set(argc - 1, argv + 1);
    if (strcmp(argv[1], "stop") == 0)   return do_pwm_stop(argc - 1, argv + 1);
    if (strcmp(argv[1], "status") == 0) return do_pwm_status(argc - 1, argv + 1);
    printf("Unknown subcommand '%s'\n", argv[1]);
    return 1;
}

void register_pwm_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "pwm",
        .help = "PWM/LEDC control: pwm <set|stop|status>",
        .hint = NULL,
        .func = &do_pwm,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
