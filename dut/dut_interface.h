/**
 * @file dut_interface.h
 * @brief UART bridge for communicating with FW-2 (Production Firmware) on DUT
 *
 * Provides command/response protocol for testing DUT peripherals via UART.
 * Protocol: FW-2 UART command specification (pfw-uart-command-spec.md)
 * Speed: 115200 baud, 8N1, no flow control
 * Port: USART1 (PB6 TX, PB7 RX on STM32L476)
 */

#ifndef DUT_INTERFACE_H
#define DUT_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== FW-2 Command Set ==================== */

/**
 * FW-2 UART Commands
 * Format: "COMMAND ARG1 ARG2...\r\n"
 * Response: "OK [VALUE]\r\n" or "ERROR [CODE]\r\n"
 */

typedef enum {
    /* GPIO control */
    FW2_CMD_GPIO_SET,           /* GPIO_SET PORT PIN STATE */
    FW2_CMD_GPIO_GET,           /* GPIO_GET PORT PIN → OK STATE */

    /* ADC reading */
    FW2_CMD_ADC_READ,           /* ADC_READ CHANNEL → OK VALUE_MV */
    FW2_CMD_ADC_INIT,           /* ADC_INIT → OK */

    /* DAC control */
    FW2_CMD_DAC_WRITE,          /* DAC_WRITE CHANNEL VALUE_MV → OK */

    /* Peripheral operations */
    FW2_CMD_UART_SEND,          /* UART_SEND PORT DATA → OK */
    FW2_CMD_UART_RECEIVE,       /* UART_RECEIVE PORT TIMEOUT_MS → OK DATA */

    /* Status and info */
    FW2_CMD_STATUS,             /* STATUS → OK STATE */
    FW2_CMD_VERSION,            /* VERSION → OK VERSION_STRING */

    FW2_CMD_MAX
} FW2Command_t;

/* ==================== GPIO Parameters ==================== */

typedef enum {
    FW2_GPIO_PORT_A = 0,
    FW2_GPIO_PORT_B = 1,
    FW2_GPIO_PORT_C = 2,
    FW2_GPIO_PORT_D = 3,
    FW2_GPIO_PORT_E = 4,
    FW2_GPIO_PORT_F = 5
} FW2_GPIO_Port_t;

typedef enum {
    FW2_GPIO_PIN_RESET = 0,
    FW2_GPIO_PIN_SET = 1
} FW2_GPIO_State_t;

/* ==================== Response Codes ==================== */

typedef enum {
    DUT_SUCCESS = 0,
    DUT_ERROR_INVALID_COMMAND = 1,
    DUT_ERROR_INVALID_ARGUMENT = 2,
    DUT_ERROR_TIMEOUT = 3,
    DUT_ERROR_PERMISSION = 4,
    DUT_ERROR_HARDWARE = 5,
    DUT_ERROR_UNKNOWN = 99
} DUT_ErrorCode_t;

typedef struct {
    bool success;
    DUT_ErrorCode_t error_code;
    uint32_t value;              /* Numeric response (e.g., ADC reading) */
    char data[256];              /* String response (e.g., version) */
} DUT_Response_t;

/* ==================== Initialization ==================== */

/**
 * @brief Initialize DUT interface
 *
 * Configures UART for communication with DUT.
 * Sets up HAL_UART for USART1 at 115200 baud.
 *
 * @return 0 on success, -1 on error
 */
int DUT_Interface_Init(void);

/**
 * @brief Deinitialize DUT interface
 *
 * @return 0 on success, -1 on error
 */
int DUT_Interface_Deinit(void);

/**
 * @brief Check if DUT is responding
 *
 * Sends STATUS command and verifies response.
 *
 * @return 1 if responsive, 0 if not, -1 on error
 */
int DUT_Interface_IsResponsive(void);

/* ==================== GPIO Commands ==================== */

/**
 * @brief Set GPIO pin on DUT
 *
 * Sends: "GPIO_SET PORT PIN STATE\r\n"
 * Example: "GPIO_SET 2 5 1\r\n" (set PC5 to HIGH)
 *
 * @param port GPIO port (0-5)
 * @param pin Pin number (0-15)
 * @param state PIN_SET (1) or PIN_RESET (0)
 * @param timeout_ms Command timeout in milliseconds
 *
 * @return 0 on success, -1 on error
 */
int DUT_Interface_GPIO_Set(uint8_t port, uint8_t pin, uint8_t state,
                           uint16_t timeout_ms);

/**
 * @brief Get GPIO pin state from DUT
 *
 * Sends: "GPIO_GET PORT PIN\r\n"
 * Returns: "OK STATE\r\n" (0 or 1)
 *
 * @param port GPIO port
 * @param pin Pin number
 * @param state Pointer to receive pin state
 * @param timeout_ms Command timeout in milliseconds
 *
 * @return 0 on success, -1 on error
 */
int DUT_Interface_GPIO_Get(uint8_t port, uint8_t pin, uint8_t* state,
                           uint16_t timeout_ms);

/* ==================== ADC Commands ==================== */

/**
 * @brief Read ADC channel from DUT
 *
 * Sends: "ADC_READ CHANNEL\r\n"
 * Returns: "OK VALUE_MV\r\n"
 *
 * @param channel ADC channel (0-15)
 * @param value_mv Pointer to receive voltage in millivolts
 * @param timeout_ms Command timeout in milliseconds
 *
 * @return 0 on success, -1 on error
 */
int DUT_Interface_ADC_Read(uint8_t channel, uint16_t* value_mv,
                           uint16_t timeout_ms);

/**
 * @brief Initialize DUT ADC
 *
 * Sends: "ADC_INIT\r\n"
 *
 * @param timeout_ms Command timeout in milliseconds
 *
 * @return 0 on success, -1 on error
 */
int DUT_Interface_ADC_Init(uint16_t timeout_ms);

/* ==================== DAC Commands ==================== */

/**
 * @brief Write DAC on DUT
 *
 * Sends: "DAC_WRITE CHANNEL VALUE_MV\r\n"
 *
 * @param channel DAC channel
 * @param value_mv Voltage in millivolts
 * @param timeout_ms Command timeout in milliseconds
 *
 * @return 0 on success, -1 on error
 */
int DUT_Interface_DAC_Write(uint8_t channel, uint16_t value_mv,
                            uint16_t timeout_ms);

/* ==================== Generic Command/Response ==================== */

/**
 * @brief Send raw command to DUT and get response
 *
 * Lower-level interface for advanced commands.
 *
 * @param command Command string (without \r\n)
 * @param response Pointer to receive response
 * @param timeout_ms Command timeout in milliseconds
 *
 * @return 0 on success, -1 on error
 */
int DUT_Interface_Command(const char* command, DUT_Response_t* response,
                          uint16_t timeout_ms);

/**
 * @brief Parse response string from DUT
 *
 * Parses "OK [VALUE]" or "ERROR [CODE]" format.
 *
 * @param response_str Response string (e.g., "OK 1234")
 * @param response Pointer to receive parsed response
 *
 * @return 0 on success, -1 on parse error
 */
int DUT_Interface_ParseResponse(const char* response_str,
                                DUT_Response_t* response);

/* ==================== Status & Diagnostics ==================== */

/**
 * @brief Get last error from DUT
 *
 * @return DUT error code
 */
DUT_ErrorCode_t DUT_Interface_GetLastError(void);

/**
 * @brief Get last response from DUT
 *
 * @return Pointer to response structure
 */
const DUT_Response_t* DUT_Interface_GetLastResponse(void);

/**
 * @brief Get FW-2 version string
 *
 * Sends: "VERSION\r\n"
 * Returns: "OK VERSION_STRING\r\n"
 *
 * @param version_str Buffer to receive version string
 * @param buffer_size Size of buffer
 * @param timeout_ms Command timeout in milliseconds
 *
 * @return 0 on success, -1 on error
 */
int DUT_Interface_GetVersion(char* version_str, size_t buffer_size,
                             uint16_t timeout_ms);

/* ==================== Advanced ==================== */

/**
 * @brief Set command timeout
 *
 * Default: 1000ms. Can be adjusted for slow operations.
 *
 * @param timeout_ms Timeout in milliseconds
 */
void DUT_Interface_SetDefaultTimeout(uint16_t timeout_ms);

/**
 * @brief Enable/disable debug logging
 *
 * Logs all commands and responses (for development).
 *
 * @param enable 1 to enable, 0 to disable
 */
void DUT_Interface_SetDebugLogging(bool enable);

#ifdef __cplusplus
}
#endif

#endif /* DUT_INTERFACE_H */
