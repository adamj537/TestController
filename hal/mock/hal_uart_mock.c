/**
 * @file hal_uart_mock.c
 * @brief UART Mock Implementation for Unit Testing
 *
 * Simulates serial communication with configurable tx/rx buffers.
 */

#include "../hal_uart.h"
#include <string.h>
#include <stdlib.h>

#define UART_MOCK_BUFFER_SIZE 256

static struct {
    struct {
        uint8_t tx_buffer[UART_MOCK_BUFFER_SIZE];
        uint16_t tx_index;
        uint8_t rx_buffer[UART_MOCK_BUFFER_SIZE];
        uint16_t rx_index;
        uint16_t rx_read_pos;
        uint32_t baudrate;
        bool initialized;
    } port[3];
} uart_mock = {0};

int HAL_UART_Init(HAL_UART_Port_t port, uint32_t baudrate,
                  HAL_UART_Parity_t parity, HAL_UART_StopBits_t stopbits) {
    if (port >= 3) return -1;

    uart_mock.port[port].baudrate = baudrate;
    uart_mock.port[port].initialized = true;
    uart_mock.port[port].tx_index = 0;
    uart_mock.port[port].rx_index = 0;
    uart_mock.port[port].rx_read_pos = 0;
    return 0;
}

int HAL_UART_Transmit(HAL_UART_Port_t port, const uint8_t* data,
                      size_t length, uint32_t timeout_ms) {
    if (port >= 3 || !uart_mock.port[port].initialized) return -1;

    size_t to_write = (uart_mock.port[port].tx_index + length <= UART_MOCK_BUFFER_SIZE) ?
                      length : (UART_MOCK_BUFFER_SIZE - uart_mock.port[port].tx_index);

    memcpy(&uart_mock.port[port].tx_buffer[uart_mock.port[port].tx_index], data, to_write);
    uart_mock.port[port].tx_index += to_write;

    return to_write;
}

int HAL_UART_TransmitString(HAL_UART_Port_t port, const char* str, uint32_t timeout_ms) {
    if (!str) return -1;
    return HAL_UART_Transmit(port, (const uint8_t*)str, strlen(str), timeout_ms);
}

int HAL_UART_Available(HAL_UART_Port_t port) {
    if (port >= 3 || !uart_mock.port[port].initialized) return -1;
    return uart_mock.port[port].rx_index - uart_mock.port[port].rx_read_pos;
}

int HAL_UART_Read(HAL_UART_Port_t port, uint32_t timeout_ms) {
    if (port >= 3 || !uart_mock.port[port].initialized) return -1;

    if (uart_mock.port[port].rx_read_pos < uart_mock.port[port].rx_index) {
        return uart_mock.port[port].rx_buffer[uart_mock.port[port].rx_read_pos++];
    }
    return -1;  /* No data */
}

int HAL_UART_ReadBytes(HAL_UART_Port_t port, uint8_t* buffer,
                       size_t length, uint32_t timeout_ms) {
    if (port >= 3 || !uart_mock.port[port].initialized || !buffer) return -1;

    int available = HAL_UART_Available(port);
    if (available <= 0) return 0;

    size_t to_read = (available < (int)length) ? available : length;
    int byte;
    for (size_t i = 0; i < to_read; i++) {
        byte = HAL_UART_Read(port, timeout_ms);
        if (byte < 0) break;
        buffer[i] = byte;
    }

    return to_read;
}

int HAL_UART_ReadUntil(HAL_UART_Port_t port, uint8_t* buffer,
                       size_t max_length, uint8_t delimiter, uint32_t timeout_ms) {
    if (port >= 3 || !uart_mock.port[port].initialized || !buffer) return -1;

    size_t count = 0;
    int byte;

    while (count < max_length) {
        byte = HAL_UART_Read(port, timeout_ms);
        if (byte < 0) break;

        buffer[count++] = byte;
        if (byte == delimiter) break;
    }

    return count;
}

int HAL_UART_FlushRx(HAL_UART_Port_t port) {
    if (port >= 3 || !uart_mock.port[port].initialized) return -1;
    uart_mock.port[port].rx_read_pos = uart_mock.port[port].rx_index;
    return 0;
}

int HAL_UART_FlushTx(HAL_UART_Port_t port) {
    if (port >= 3 || !uart_mock.port[port].initialized) return -1;
    /* Mock has no buffering, so nothing to do */
    return 0;
}

int HAL_UART_RegisterRxCallback(HAL_UART_Port_t port, HAL_UART_RxCallback_t callback) {
    if (port >= 3 || !uart_mock.port[port].initialized) return -1;
    /* Mock doesn't use callbacks */
    return 0;
}

/* Mock access for testing */
uint8_t* HAL_UART_Mock_GetTxBuffer(HAL_UART_Port_t port) {
    if (port >= 3) return NULL;
    return uart_mock.port[port].tx_buffer;
}

uint16_t HAL_UART_Mock_GetTxLength(HAL_UART_Port_t port) {
    if (port >= 3) return 0;
    return uart_mock.port[port].tx_index;
}

void HAL_UART_Mock_SetRxBuffer(HAL_UART_Port_t port, const uint8_t* data, size_t length) {
    if (port >= 3 || length > UART_MOCK_BUFFER_SIZE) return;
    memcpy(uart_mock.port[port].rx_buffer, data, length);
    uart_mock.port[port].rx_index = length;
    uart_mock.port[port].rx_read_pos = 0;
}

void HAL_UART_Mock_Reset(void) {
    memset(&uart_mock, 0, sizeof(uart_mock));
}
