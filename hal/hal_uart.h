/**
 * @file hal_uart.h
 * @brief UART Hardware Abstraction Layer
 *
 * Serial communication for DUT interface and debug output.
 * Implementations: hal_uart_esp32.c, hal_uart_mock.c
 */

#ifndef HAL_UART_H
#define HAL_UART_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Type Definitions ==================== */

typedef enum {
    HAL_UART_PORT_0 = 0,  /* Debug UART on ESP32 (USB) */
    HAL_UART_PORT_1 = 1,  /* DUT UART on ESP32 */
    HAL_UART_PORT_2 = 2   /* Alternate UART (future use) */
} HAL_UART_Port_t;

typedef enum {
    HAL_UART_PARITY_NONE = 0,
    HAL_UART_PARITY_ODD = 1,
    HAL_UART_PARITY_EVEN = 2
} HAL_UART_Parity_t;

typedef enum {
    HAL_UART_STOPBITS_1 = 1,
    HAL_UART_STOPBITS_2 = 2
} HAL_UART_StopBits_t;

typedef void (*HAL_UART_RxCallback_t)(uint8_t byte);

/* ==================== Core UART Operations ==================== */

/**
 * @brief Initialize UART port
 *
 * @param port UART port number
 * @param baudrate Baud rate (9600, 115200, etc.)
 * @param parity Parity mode
 * @param stopbits Stop bits (1 or 2)
 *
 * @return 0 on success, -1 on error
 */
int HAL_UART_Init(HAL_UART_Port_t port, uint32_t baudrate,
                  HAL_UART_Parity_t parity, HAL_UART_StopBits_t stopbits);

/**
 * @brief Transmit data via UART (blocking)
 *
 * @param port UART port
 * @param data Pointer to data buffer
 * @param length Number of bytes to send
 * @param timeout_ms Timeout in milliseconds (0 = infinite)
 *
 * @return Number of bytes sent, or -1 on error
 */
int HAL_UART_Transmit(HAL_UART_Port_t port, const uint8_t* data,
                      size_t length, uint32_t timeout_ms);

/**
 * @brief Transmit null-terminated string via UART
 *
 * @param port UART port
 * @param str Pointer to string
 * @param timeout_ms Timeout in milliseconds
 *
 * @return Number of bytes sent, or -1 on error
 */
int HAL_UART_TransmitString(HAL_UART_Port_t port, const char* str, uint32_t timeout_ms);

/**
 * @brief Check if data is available to read
 *
 * @param port UART port
 *
 * @return Number of bytes available, 0 if none, -1 on error
 */
int HAL_UART_Available(HAL_UART_Port_t port);

/**
 * @brief Read single byte from UART (blocking)
 *
 * @param port UART port
 * @param timeout_ms Timeout in milliseconds
 *
 * @return Byte read (0-255), or -1 on error/timeout
 */
int HAL_UART_Read(HAL_UART_Port_t port, uint32_t timeout_ms);

/**
 * @brief Read multiple bytes from UART (blocking)
 *
 * @param port UART port
 * @param buffer Output buffer
 * @param length Maximum bytes to read
 * @param timeout_ms Timeout in milliseconds
 *
 * @return Number of bytes read, or -1 on error
 */
int HAL_UART_ReadBytes(HAL_UART_Port_t port, uint8_t* buffer,
                       size_t length, uint32_t timeout_ms);

/**
 * @brief Read until delimiter (e.g., \\r\\n)
 *
 * @param port UART port
 * @param buffer Output buffer
 * @param max_length Maximum bytes to read
 * @param delimiter Byte to stop at (e.g., '\\n')
 * @param timeout_ms Timeout in milliseconds
 *
 * @return Number of bytes read (including delimiter), or -1 on error
 */
int HAL_UART_ReadUntil(HAL_UART_Port_t port, uint8_t* buffer,
                       size_t max_length, uint8_t delimiter, uint32_t timeout_ms);

/**
 * @brief Flush RX buffer (discard all pending data)
 *
 * @param port UART port
 *
 * @return 0 on success, -1 on error
 */
int HAL_UART_FlushRx(HAL_UART_Port_t port);

/**
 * @brief Flush TX buffer (wait for all data to be sent)
 *
 * @param port UART port
 *
 * @return 0 on success, -1 on error
 */
int HAL_UART_FlushTx(HAL_UART_Port_t port);

/* ==================== Interrupt Operations ==================== */

/**
 * @brief Register RX interrupt callback
 *
 * Called for each byte received (interrupt context).
 *
 * @param port UART port
 * @param callback Function to call (or NULL to disable)
 *
 * @return 0 on success, -1 on error
 */
int HAL_UART_RegisterRxCallback(HAL_UART_Port_t port, HAL_UART_RxCallback_t callback);

#ifdef __cplusplus
}
#endif

#endif /* HAL_UART_H */
