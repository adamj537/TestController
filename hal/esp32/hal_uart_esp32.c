/**
 * @file hal_uart_esp32.c
 * @brief ESP32-specific UART HAL implementation
 *
 * Uses ESP-IDF UART driver to implement platform-independent serial interface.
 * ESP32 has 3 UART ports (UART0, UART1, UART2).
 */

#include "../hal_uart.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/* ==================== Configuration ==================== */

#define UART_BUF_SIZE 1024

typedef struct {
    uart_port_t uart_port;
    bool initialized;
    uint32_t baudrate;
    uint16_t timeout_ms;
} UART_State_t;

static UART_State_t uart_state[HAL_UART_PORT_MAX] = {
    {UART_NUM_0, false, 115200, 100},
    {UART_NUM_1, false, 115200, 100},
    {UART_NUM_2, false, 115200, 100},
};

/* ==================== Port Mapping ==================== */

static uart_port_t hal_port_to_esp(HAL_UART_Port_t port) {
    if (port >= HAL_UART_PORT_MAX) {
        return UART_NUM_MAX;
    }
    return uart_state[port].uart_port;
}

static bool is_valid_port(HAL_UART_Port_t port) {
    return port < HAL_UART_PORT_MAX;
}

/* ==================== Initialization ==================== */

int HAL_UART_Init(HAL_UART_Port_t port, uint32_t baudrate) {
    if (!is_valid_port(port)) return -1;

    uart_port_t uart_num = hal_port_to_esp(port);
    if (uart_num == UART_NUM_MAX) return -1;

    if (uart_state[port].initialized) return 0;

    /* Configure UART parameters */
    uart_config_t uart_config = {
        .baud_rate = baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
    };

    /* Configure UART */
    if (uart_param_config(uart_num, &uart_config) != ESP_OK) {
        return -1;
    }

    /* Set UART pins (using defaults for each port) */
    int tx_pin, rx_pin;

    switch (uart_num) {
        case UART_NUM_0:
            tx_pin = UART_PIN_NO_CHANGE;  /* TX on GPIO1 by default */
            rx_pin = UART_PIN_NO_CHANGE;  /* RX on GPIO3 by default */
            break;
        case UART_NUM_1:
            tx_pin = GPIO_NUM_10;         /* TX on GPIO10 */
            rx_pin = GPIO_NUM_9;          /* RX on GPIO9 */
            break;
        case UART_NUM_2:
            tx_pin = GPIO_NUM_17;         /* TX on GPIO17 */
            rx_pin = GPIO_NUM_16;         /* RX on GPIO16 */
            break;
        default:
            return -1;
    }

    if (uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        return -1;
    }

    /* Install UART driver and allocate UART buffer */
    if (uart_driver_install(uart_num, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0) != ESP_OK) {
        return -1;
    }

    uart_state[port].initialized = true;
    uart_state[port].baudrate = baudrate;

    return 0;
}

int HAL_UART_Deinit(HAL_UART_Port_t port) {
    if (!is_valid_port(port)) return -1;

    uart_port_t uart_num = hal_port_to_esp(port);
    if (uart_num == UART_NUM_MAX) return -1;

    if (!uart_state[port].initialized) return -1;

    uart_driver_delete(uart_num);
    uart_state[port].initialized = false;

    return 0;
}

/* ==================== Basic I/O ==================== */

int HAL_UART_Transmit(HAL_UART_Port_t port, const uint8_t* data, size_t length,
                     uint16_t timeout_ms) {
    if (!is_valid_port(port) || !data || length == 0) return -1;

    uart_port_t uart_num = hal_port_to_esp(port);
    if (uart_num == UART_NUM_MAX || !uart_state[port].initialized) return -1;

    int bytes = uart_write_bytes(uart_num, data, length);

    if (bytes != (int)length) {
        return -1;
    }

    return 0;
}

int HAL_UART_Receive(HAL_UART_Port_t port, uint8_t* data, size_t length,
                    uint16_t timeout_ms) {
    if (!is_valid_port(port) || !data || length == 0) return -1;

    uart_port_t uart_num = hal_port_to_esp(port);
    if (uart_num == UART_NUM_MAX || !uart_state[port].initialized) return -1;

    int bytes = uart_read_bytes(uart_num, data, length, timeout_ms / portTICK_PERIOD_MS);

    return bytes > 0 ? bytes : -1;
}

/* ==================== Line-Based I/O ==================== */

int HAL_UART_TransmitLine(HAL_UART_Port_t port, const char* line,
                         uint16_t timeout_ms) {
    if (!is_valid_port(port) || !line) return -1;

    size_t line_len = strlen(line);

    /* Send line */
    if (HAL_UART_Transmit(port, (const uint8_t*)line, line_len, timeout_ms) != 0) {
        return -1;
    }

    /* Send \r\n */
    const char* crlf = "\r\n";
    if (HAL_UART_Transmit(port, (const uint8_t*)crlf, 2, timeout_ms) != 0) {
        return -1;
    }

    return 0;
}

int HAL_UART_ReceiveLine(HAL_UART_Port_t port, char* buffer, size_t buffer_size,
                        uint16_t timeout_ms) {
    if (!is_valid_port(port) || !buffer || buffer_size == 0) return -1;

    uart_port_t uart_num = hal_port_to_esp(port);
    if (uart_num == UART_NUM_MAX || !uart_state[port].initialized) return -1;

    size_t pos = 0;
    uint32_t deadline = xTaskGetTickCount() + (timeout_ms / portTICK_PERIOD_MS);

    while (pos < buffer_size - 1) {
        int bytes = uart_read_bytes(uart_num, (uint8_t*)&buffer[pos], 1, 10);

        if (bytes == 1) {
            /* Check for line termination */
            if (buffer[pos] == '\n') {
                /* Found newline - remove trailing \r if present */
                if (pos > 0 && buffer[pos - 1] == '\r') {
                    pos--;
                }
                buffer[pos] = '\0';
                return pos;
            }
            pos++;
        } else {
            /* Check timeout */
            if (xTaskGetTickCount() >= deadline) {
                return -1;  /* Timeout */
            }

            /* Small delay to avoid busy-waiting */
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
    }

    buffer[pos] = '\0';
    return pos;
}

/* ==================== Configuration ==================== */

int HAL_UART_SetTimeout(HAL_UART_Port_t port, uint16_t timeout_ms) {
    if (!is_valid_port(port)) return -1;

    uart_state[port].timeout_ms = timeout_ms;
    return 0;
}

uint16_t HAL_UART_GetTimeout(HAL_UART_Port_t port) {
    if (!is_valid_port(port)) return 0;

    return uart_state[port].timeout_ms;
}

int HAL_UART_SetBaudrate(HAL_UART_Port_t port, uint32_t baudrate) {
    if (!is_valid_port(port) || !uart_state[port].initialized) return -1;

    uart_port_t uart_num = hal_port_to_esp(port);

    if (uart_set_baudrate(uart_num, baudrate) != ESP_OK) {
        return -1;
    }

    uart_state[port].baudrate = baudrate;
    return 0;
}

uint32_t HAL_UART_GetBaudrate(HAL_UART_Port_t port) {
    if (!is_valid_port(port)) return 0;

    return uart_state[port].baudrate;
}

/* ==================== Status ==================== */

int HAL_UART_IsInitialized(HAL_UART_Port_t port) {
    if (!is_valid_port(port)) return -1;

    return uart_state[port].initialized ? 1 : 0;
}

int HAL_UART_GetAvailable(HAL_UART_Port_t port) {
    if (!is_valid_port(port) || !uart_state[port].initialized) return -1;

    uart_port_t uart_num = hal_port_to_esp(port);

    size_t available = 0;
    if (uart_get_buffered_data_len(uart_num, &available) != ESP_OK) {
        return -1;
    }

    return available;
}

int HAL_UART_Flush(HAL_UART_Port_t port) {
    if (!is_valid_port(port) || !uart_state[port].initialized) return -1;

    uart_port_t uart_num = hal_port_to_esp(port);

    if (uart_flush(uart_num) != ESP_OK) {
        return -1;
    }

    return 0;
}

/* ==================== ESP32-Specific ==================== */

/**
 * @brief Get ESP-IDF UART port number
 *
 * Useful for direct UART driver access if needed.
 *
 * @param port HAL UART port
 *
 * @return ESP-IDF uart_port_t, or UART_NUM_MAX if invalid
 */
uart_port_t HAL_UART_ESP32_GetUartNum(HAL_UART_Port_t port) {
    if (!is_valid_port(port)) return UART_NUM_MAX;

    return hal_port_to_esp(port);
}
