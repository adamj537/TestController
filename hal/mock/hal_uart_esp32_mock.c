/**
 * @file hal_uart_esp32_mock.c
 * @brief Mock ESP32 UART HAL for Linux testing
 *
 * Simulates UART communication without ESP-IDF dependencies.
 * Allows DUT interface protocol testing on Linux.
 */

#include "../hal_uart.h"
#include <string.h>
#include <stdio.h>

/* ==================== Mock State ==================== */

#define UART_BUFFER_SIZE 1024

typedef struct {
    bool initialized;
    uint32_t baudrate;
    uint32_t timeout_ms;
    HAL_UART_Parity_t parity;
    HAL_UART_StopBits_t stopbits;
    char tx_buffer[UART_BUFFER_SIZE];
    char rx_buffer[UART_BUFFER_SIZE];
    size_t rx_pos;
    size_t rx_available;
} UART_Mock_t;

static UART_Mock_t uart_mock[3] = {0};

/* Mock debug logging */
#ifdef DEBUG
#define UART_LOG(fmt, ...) printf("[UART] " fmt "\n", ##__VA_ARGS__)
#else
#define UART_LOG(fmt, ...) do {} while(0)
#endif

/* ==================== Initialization ==================== */

int HAL_UART_Init(HAL_UART_Port_t port, uint32_t baudrate,
                  HAL_UART_Parity_t parity, HAL_UART_StopBits_t stopbits) {
    if (port >= 3) return -1;

    if (uart_mock[port].initialized) return 0;

    uart_mock[port].baudrate = baudrate;
    uart_mock[port].parity = parity;
    uart_mock[port].stopbits = stopbits;
    uart_mock[port].timeout_ms = 1000;
    uart_mock[port].rx_pos = 0;
    uart_mock[port].rx_available = 0;
    uart_mock[port].initialized = true;

    UART_LOG("Init port %d at %d baud (parity %d, stopbits %d)", port, baudrate, parity, stopbits);
    return 0;
}

int HAL_UART_Deinit(HAL_UART_Port_t port) {
    if (port >= 3 || !uart_mock[port].initialized) return -1;

    uart_mock[port].initialized = false;
    UART_LOG("Deinit port %d", port);
    return 0;
}

/* ==================== Basic I/O ==================== */

int HAL_UART_Transmit(HAL_UART_Port_t port, const uint8_t* data,
                      size_t length, uint32_t timeout_ms) {
    if (port >= 3 || !uart_mock[port].initialized || !data || length == 0) return -1;

    if (length > UART_BUFFER_SIZE) length = UART_BUFFER_SIZE;

    memcpy(uart_mock[port].tx_buffer, data, length);
    uart_mock[port].tx_buffer[length] = '\0';

    UART_LOG("Transmit port %d (%zu bytes): %.*s", port, length, (int)length,
             (const char*)data);
    return (int)length;
}

int HAL_UART_TransmitString(HAL_UART_Port_t port, const char* str, uint32_t timeout_ms) {
    if (port >= 3 || !uart_mock[port].initialized || !str) return -1;

    size_t len = strlen(str);
    return HAL_UART_Transmit(port, (const uint8_t*)str, len, timeout_ms);
}

int HAL_UART_Available(HAL_UART_Port_t port) {
    if (port >= 3 || !uart_mock[port].initialized) return -1;
    return (int)uart_mock[port].rx_available;
}

int HAL_UART_Read(HAL_UART_Port_t port, uint32_t timeout_ms) {
    if (port >= 3 || !uart_mock[port].initialized) return -1;

    if (uart_mock[port].rx_available == 0) {
        UART_LOG("Read port %d: no data", port);
        return -1;  /* No data available */
    }

    uint8_t byte = uart_mock[port].rx_buffer[uart_mock[port].rx_pos];
    uart_mock[port].rx_pos++;
    uart_mock[port].rx_available--;

    UART_LOG("Read port %d: 0x%02x", port, byte);
    return (int)byte;
}

int HAL_UART_ReadBytes(HAL_UART_Port_t port, uint8_t* data, size_t length,
                       uint32_t timeout_ms) {
    if (port >= 3 || !uart_mock[port].initialized || !data || length == 0) return -1;

    if (uart_mock[port].rx_available == 0) {
        UART_LOG("ReadBytes port %d: no data", port);
        return -1;  /* No data available */
    }

    size_t to_read = length < uart_mock[port].rx_available ? length : uart_mock[port].rx_available;
    memcpy(data, &uart_mock[port].rx_buffer[uart_mock[port].rx_pos], to_read);

    uart_mock[port].rx_pos += to_read;
    uart_mock[port].rx_available -= to_read;

    UART_LOG("ReadBytes port %d (%zu bytes)", port, to_read);
    return (int)to_read;
}

/* ==================== Status ==================== */

int HAL_UART_IsInitialized(HAL_UART_Port_t port) {
    if (port >= 3) return -1;
    return uart_mock[port].initialized ? 1 : 0;
}

/* ==================== Mock Helpers (for testing) ==================== */

/**
 * @brief Inject received data to simulate DUT responses
 */
int HAL_UART_Mock_InjectRxData(HAL_UART_Port_t port, const char* data) {
    if (port >= 3 || !data) return -1;

    size_t len = strlen(data);
    if (len > UART_BUFFER_SIZE) return -1;

    uart_mock[port].rx_pos = 0;
    memcpy(uart_mock[port].rx_buffer, data, len);
    uart_mock[port].rx_available = len;

    UART_LOG("Inject RX data port %d: %s", port, data);
    return 0;
}

/**
 * @brief Get last transmitted data
 */
const char* HAL_UART_Mock_GetTxData(HAL_UART_Port_t port) {
    return port < 3 ? uart_mock[port].tx_buffer : NULL;
}

/**
 * @brief Reset mock state
 */
void HAL_UART_Mock_Reset(void) {
    memset(&uart_mock, 0, sizeof(uart_mock));
    UART_LOG("Reset all UART mock state");
}
