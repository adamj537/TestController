#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_console.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "cmd_uart.h"

/* DUT UART lines connect to TCC GPIO43 (TX→DUT RX) and GPIO44 (RX←DUT TX).
 * We use UART1 for monitoring (RX only on GPIO44) so we don't compete with
 * the UART0 console.  UART0 RX FIFO is flushed on exit so DUT bytes don't
 * get replayed as shell commands when the REPL resumes. */

#define MON_PORT     UART_NUM_1
#define MON_RX_GPIO  44          /* DUT TX → TCC GPIO44 */
#define MON_TX_GPIO  43          /* TCC TX → DUT RX      */
#define MON_BUF_SZ   2048

static int do_uart_mon(int argc, char **argv)
{
    int timeout_ms = 5000;
    int baud       = 115200;

    if (argc >= 2) timeout_ms = atoi(argv[1]);
    if (argc >= 3) baud       = atoi(argv[2]);

    printf("=== uart mon  baud=%d  timeout=%dms — waiting for DUT output ===\n",
           baud, timeout_ms);
    fflush(stdout);

    uart_config_t cfg = {
        .baud_rate = baud,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    if (uart_driver_install(MON_PORT, MON_BUF_SZ, 0, 0, NULL, 0) != ESP_OK) {
        printf("uart_driver_install failed\n");
        return 1;
    }
    if (uart_param_config(MON_PORT, &cfg) != ESP_OK ||
        /* RX only — leave TX unassigned to avoid contention on GPIO43 */
        uart_set_pin(MON_PORT, UART_PIN_NO_CHANGE, MON_RX_GPIO,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        printf("uart config/pin failed\n");
        uart_driver_delete(MON_PORT);
        return 1;
    }

    uint8_t buf[256];
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    int total = 0;

    while (esp_timer_get_time() < deadline) {
        int n = uart_read_bytes(MON_PORT, buf, sizeof(buf), pdMS_TO_TICKS(50));
        if (n > 0) {
            fwrite(buf, 1, n, stdout);
            fflush(stdout);
            total += n;
        }
    }

    uart_driver_delete(MON_PORT);

    printf("\n=== monitor done  %d bytes received ===\n", total);
    return 0;
}

/* uart cmd <text> [timeout_ms] [baud]
 * Send <text>\r\n to DUT and print the response. */
static int do_uart_cmd(int argc, char **argv)
{
    if (argc < 2) { printf("Usage: uart cmd <text> [timeout_ms] [baud]\n"); return 1; }
    const char *text   = argv[1];
    int timeout_ms     = (argc >= 3) ? atoi(argv[2]) : 2000;
    int baud           = (argc >= 4) ? atoi(argv[3]) : 115200;

    uart_config_t cfg = {
        .baud_rate = baud,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    if (uart_driver_install(MON_PORT, MON_BUF_SZ, MON_BUF_SZ, 0, NULL, 0) != ESP_OK) {
        printf("uart_driver_install failed\n"); return 1;
    }
    if (uart_param_config(MON_PORT, &cfg) != ESP_OK ||
        uart_set_pin(MON_PORT, MON_TX_GPIO, MON_RX_GPIO,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        printf("uart config/pin failed\n");
        uart_driver_delete(MON_PORT); return 1;
    }

    /* send command with \r\n terminator */
    uart_write_bytes(MON_PORT, text, strlen(text));
    uart_write_bytes(MON_PORT, "\r\n", 2);
    uart_wait_tx_done(MON_PORT, pdMS_TO_TICKS(500));

    /* read response */
    uint8_t buf[256];
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    int total = 0;
    while (esp_timer_get_time() < deadline) {
        int n = uart_read_bytes(MON_PORT, buf, sizeof(buf) - 1, pdMS_TO_TICKS(50));
        if (n > 0) { buf[n] = 0; printf("%s", buf); fflush(stdout); total += n; }
    }
    uart_driver_delete(MON_PORT);
    if (total == 0) printf("(no response)\n");
    return 0;
}

static int do_uart(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "mon") == 0)
        return do_uart_mon(argc - 1, argv + 1);
    if (argc >= 2 && strcmp(argv[1], "cmd") == 0)
        return do_uart_cmd(argc - 1, argv + 1);

    printf("Usage:\n");
    printf("  uart mon <timeout_ms> [baud]        dump DUT UART output\n");
    printf("  uart cmd <text> [timeout_ms] [baud]  send command, print response\n");
    return 1;
}

void register_uart_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "uart",
        .help    = "uart mon <timeout_ms> [baud]  — capture DUT UART output",
        .hint    = NULL,
        .func    = &do_uart,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
