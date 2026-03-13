#include "dut_identify.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <ctype.h>

static const char *TAG = "dut_id";

#define MON_PORT     UART_NUM_1
#define MON_RX_GPIO  44
#define MON_BUF_SZ   2048
#define MIN_HEX_RUN  8    /* minimum hex chars to qualify as a UID token */

static bool is_hex(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
}

/* Scan buf for the longest run of hex chars >= MIN_HEX_RUN.
 * Copies it (null-terminated) into out[], truncated to out_sz - 1.
 * Returns length written, or 0 if not found. */
static int extract_hex_token(const char *buf, int len, char *out, size_t out_sz)
{
    const char *best_start = NULL;
    int best_len = 0;

    const char *run_start = NULL;
    int run_len = 0;

    for (int i = 0; i <= len; i++) {
        char c = (i < len) ? buf[i] : 0;
        if (is_hex(c)) {
            if (!run_start) { run_start = &buf[i]; run_len = 0; }
            run_len++;
        } else {
            if (run_start && run_len >= MIN_HEX_RUN) {
                if (run_len > best_len) {
                    best_start = run_start;
                    best_len   = run_len;
                }
            }
            run_start = NULL;
            run_len   = 0;
        }
    }

    if (!best_start || best_len < MIN_HEX_RUN) return 0;

    int copy = (best_len < (int)(out_sz - 1)) ? best_len : (int)(out_sz - 1);
    memcpy(out, best_start, copy);
    out[copy] = '\0';

    /* Normalise to uppercase */
    for (int i = 0; i < copy; i++) {
        out[i] = (char)toupper((unsigned char)out[i]);
    }

    return copy;
}

int dut_identify_uart(char *out, size_t out_sz, int timeout_ms)
{
    out[0] = '\0';

    uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    esp_err_t err = uart_driver_install(MON_PORT, MON_BUF_SZ, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return 0;
    }

    if (uart_param_config(MON_PORT, &cfg) != ESP_OK ||
        uart_set_pin(MON_PORT, UART_PIN_NO_CHANGE, MON_RX_GPIO,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGW(TAG, "uart config/pin failed");
        uart_driver_delete(MON_PORT);
        return 0;
    }

    /* Accumulate all bytes received within the window */
    static char rxbuf[MON_BUF_SZ];
    int total = 0;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000LL;

    while (esp_timer_get_time() < deadline && total < (int)sizeof(rxbuf) - 1) {
        int n = uart_read_bytes(MON_PORT, (uint8_t *)&rxbuf[total],
                                sizeof(rxbuf) - 1 - total, pdMS_TO_TICKS(50));
        if (n > 0) total += n;
    }

    uart_driver_delete(MON_PORT);
    rxbuf[total] = '\0';

    if (total == 0) {
        ESP_LOGW(TAG, "no UART output from DUT in %d ms", timeout_ms);
        return 0;
    }

    ESP_LOGI(TAG, "DUT UART: %d bytes received", total);

    int found = extract_hex_token(rxbuf, total, out, out_sz);
    if (found) {
        ESP_LOGI(TAG, "DUT serial candidate: %s (%d chars)", out, found);
    } else {
        ESP_LOGW(TAG, "no hex UID token found in DUT UART output");
    }

    return found;
}
