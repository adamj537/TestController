#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_console.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "cmd_adc.h"

static const char *TAG = "cmd_adc";

static adc_oneshot_unit_handle_t s_adc1 = NULL;
static adc_cali_handle_t s_cali = NULL;
static bool s_cali_ok = false;

static void ensure_adc_init(void)
{
    if (s_adc1 != NULL) return;

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &s_adc1));

    /* Try curve-fitting calibration first (uses eFuse factory data) */
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK) {
        s_cali_ok = true;
        ESP_LOGI(TAG, "ADC calibration: curve fitting");
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!s_cali_ok) {
        adc_cali_line_fitting_config_t cali_cfg = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_cali_create_scheme_line_fitting(&cali_cfg, &s_cali) == ESP_OK) {
            s_cali_ok = true;
            ESP_LOGI(TAG, "ADC calibration: line fitting");
        }
    }
#endif

    if (!s_cali_ok) {
        ESP_LOGW(TAG, "ADC calibration not available — raw counts only");
    }
}

static int read_channel(int ch)
{
    adc_channel_t channel = (adc_channel_t)ch;

    adc_oneshot_chan_cfg_t cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,   /* 0–3.3 V range */
    };
    esp_err_t err = adc_oneshot_config_channel(s_adc1, channel, &cfg);
    if (err != ESP_OK) {
        printf("ADC1 CH%d config error: %s\n", ch, esp_err_to_name(err));
        return 1;
    }

    int raw;
    err = adc_oneshot_read(s_adc1, channel, &raw);
    if (err != ESP_OK) {
        printf("ADC1 CH%d read error: %s\n", ch, esp_err_to_name(err));
        return 1;
    }

    if (s_cali_ok) {
        int mv;
        adc_cali_raw_to_voltage(s_cali, raw, &mv);
        printf("ADC1 CH%d: raw=%-5d  %4d mV\n", ch, raw, mv);
    } else {
        printf("ADC1 CH%d: raw=%-5d  (no cal)\n", ch, raw);
    }
    return 0;
}

static int do_adc_read(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: adc read <channel> [channel...]\n");
        printf("  channel: ADC1 CH0-CH9 (maps to GPIO1-GPIO10 on ESP32-S3)\n");
        printf("  Attenuation: 11 dB (0-3.3V range)\n");
        return 1;
    }

    ensure_adc_init();

    for (int i = 1; i < argc; i++) {
        int ch = atoi(argv[i]);
        if (ch < 0 || ch > 9) {
            printf("Channel %d out of range (0-9)\n", ch);
            continue;
        }
        read_channel(ch);
    }
    return 0;
}

static int do_adc(int argc, char **argv)
{
    if (argc < 2) {
        printf("ADC read commands:\n");
        printf("  adc read <ch> [ch...]  read ADC1 channel(s), raw + mV\n");
        printf("  Channels 0-9 map to GPIO1-GPIO10 on ESP32-S3\n");
        return 1;
    }
    if (strcmp(argv[1], "read") == 0) return do_adc_read(argc - 1, argv + 1);
    printf("Unknown subcommand '%s'\n", argv[1]);
    return 1;
}

/* ── Selftest API ─────────────────────────────────────────────────────────── */

bool adc_selftest_read(int channel, int *raw_out, int *mv_out)
{
    ensure_adc_init();
    adc_oneshot_chan_cfg_t cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,
    };
    if (adc_oneshot_config_channel(s_adc1, (adc_channel_t)channel, &cfg) != ESP_OK) return false;
    if (adc_oneshot_read(s_adc1, (adc_channel_t)channel, raw_out) != ESP_OK) return false;
    if (s_cali_ok && mv_out) {
        adc_cali_raw_to_voltage(s_cali, *raw_out, mv_out);
    } else if (mv_out) {
        *mv_out = -1;
    }
    return true;
}

void register_adc_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "adc",
        .help = "ADC read: adc read <ch0> [ch1 ...]",
        .hint = NULL,
        .func = &do_adc,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
