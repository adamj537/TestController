/* cmd_tie.c — "tie" console command: direct TIE MUX channel read.
 *
 * Exposes a single-channel read primitive that waits a full ADC128 scan cycle
 * before sampling.  Replaces run_mux_scan() / "selftest mux" for targeted
 * per-channel reads in test scripts and recipes.
 *
 * Command:
 *   tie read <mux 0-3> <ch 0-15> [settle_ms] [samples]
 *
 * Output (one parseable line):
 *   TIE <mux> <ch> <mv> mV
 *
 * Defaults: settle_ms=110 (> one full ADC128 scan at ~12ms/ch × 8ch = 96ms),
 *           samples=1.  When samples > 1, the average is reported.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cmd_selftest.h"
#include "cmd_tie.h"

static const char *TAG = "cmd_tie";

/* > one full ADC128D818 high-rate scan: 8 ch × ~12 ms/ch = ~96 ms */
#define TIE_DEFAULT_SETTLE_MS  110
#define TIE_DEFAULT_SAMPLES      1
#define TIE_MAX_SAMPLES         16

/* ── tie read ──────────────────────────────────────────────────────────────── */

static int do_tie_read(int argc, char **argv)
{
    /* argv[0]="read"  argv[1]=mux  argv[2]=ch  [argv[3]=settle_ms  argv[4]=samples] */
    if (argc < 3) {
        printf("Usage: tie read <mux 0-3> <ch 0-15> [settle_ms] [samples]\n");
        printf("  settle_ms default=%d  samples default=%d (max %d)\n",
               TIE_DEFAULT_SETTLE_MS, TIE_DEFAULT_SAMPLES, TIE_MAX_SAMPLES);
        printf("  Output: TIE <mux> <ch> <mv> mV\n");
        return 1;
    }

    int mux       = atoi(argv[1]);
    int ch        = atoi(argv[2]);
    int settle_ms = (argc >= 4) ? atoi(argv[3]) : TIE_DEFAULT_SETTLE_MS;
    int samples   = (argc >= 5) ? atoi(argv[4]) : TIE_DEFAULT_SAMPLES;

    if (mux < 0 || mux > 3) {
        printf("ERROR: mux must be 0-3 (got %d)\n", mux);
        return 1;
    }
    if (ch < 0 || ch > 15) {
        printf("ERROR: ch must be 0-15 (got %d)\n", ch);
        return 1;
    }
    if (settle_ms < 1 || settle_ms > 5000) {
        printf("ERROR: settle_ms must be 1-5000 (got %d)\n", settle_ms);
        return 1;
    }
    if (samples < 1 || samples > TIE_MAX_SAMPLES) {
        printf("ERROR: samples must be 1-%d (got %d)\n", TIE_MAX_SAMPLES, samples);
        return 1;
    }

    if (!tie_adc128_init()) {
        printf("ERROR: ADC128 init failed\n");
        return 1;
    }

    /* MUX0 → ADC128 CH0, MUX1 → CH1, MUX2 → CH2, MUX3 → CH3 */
    uint8_t adc_ch = (uint8_t)mux;

    tie_mux_select(mux, ch);
    vTaskDelay(pdMS_TO_TICKS(settle_ms));

    int32_t sum   = 0;
    int     reads = 0;
    for (int i = 0; i < samples; i++) {
        if (i > 0) {
            /* Allow a fresh conversion to complete between samples */
            vTaskDelay(pdMS_TO_TICKS(15));
        }
        int mv = 0;
        if (tie_adc128_read_raw_mv(adc_ch, &mv)) {
            sum += mv;
            reads++;
        } else {
            ESP_LOGW(TAG, "sample %d: ADC read failed (mux=%d ch=%d)", i, mux, ch);
        }
    }

    if (reads == 0) {
        printf("ERROR: all %d ADC reads failed (mux=%d ch=%d)\n", samples, mux, ch);
        return 1;
    }

    int avg_mv = (int)(sum / reads);
    printf("TIE %d %d %d mV\n", mux, ch, avg_mv);
    return 0;
}

/* ── Command dispatcher ──────────────────────────────────────────────────── */

static int do_tie(int argc, char **argv)
{
    if (argc < 2) {
        printf("TIE MUX read commands:\n");
        printf("  tie read <mux 0-3> <ch 0-15> [settle_ms] [samples]\n");
        printf("    Read one TIE MUX channel via ADC128D818.\n");
        printf("    settle_ms default=%d  samples default=1\n", TIE_DEFAULT_SETTLE_MS);
        printf("    Output: TIE <mux> <ch> <mv> mV\n");
        return 1;
    }
    if (strcmp(argv[1], "read") == 0) return do_tie_read(argc - 1, argv + 1);
    printf("ERROR: unknown subcommand '%s'\n", argv[1]);
    return 1;
}

void register_tie_commands(void)
{
    static const esp_console_cmd_t cmd = {
        .command  = "tie",
        .help     = "TIE MUX read: tie read <mux 0-3> <ch 0-15> [settle_ms] [samples]",
        .hint     = NULL,
        .func     = &do_tie,
        .argtable = NULL,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
