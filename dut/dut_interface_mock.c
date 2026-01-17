/**
 * @file dut_interface_mock.c
 * @brief Mock DUT interface for unit testing
 *
 * Simulates FW-2 responses using mock UART.
 * Allows testing without real DUT hardware.
 */

#include "dut_interface.h"
#include "../hal/hal_uart.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ==================== State Management ==================== */

static struct {
    bool initialized;
    DUT_Response_t last_response;
    DUT_ErrorCode_t last_error;
    uint16_t default_timeout_ms;
    bool debug_logging;
} dut_state = {false, {}, DUT_SUCCESS, 1000, false};

/* ==================== Initialization ==================== */

int DUT_Interface_Init(void) {
    if (dut_state.initialized) return 0;

    /* Initialize UART for DUT communication */
    if (HAL_UART_Init(HAL_UART_PORT_1, 115200) != 0) {
        return -1;
    }

    dut_state.initialized = true;
    dut_state.default_timeout_ms = 1000;
    dut_state.debug_logging = false;

    return 0;
}

int DUT_Interface_Deinit(void) {
    if (!dut_state.initialized) return -1;

    HAL_UART_Deinit(HAL_UART_PORT_1);
    dut_state.initialized = false;

    return 0;
}

int DUT_Interface_IsResponsive(void) {
    if (!dut_state.initialized) return -1;

    DUT_Response_t response;
    int result = DUT_Interface_Command("STATUS", &response, 500);

    return (result == 0) ? 1 : 0;
}

/* ==================== Command Execution ==================== */

int DUT_Interface_Command(const char* command, DUT_Response_t* response,
                          uint16_t timeout_ms) {
    if (!dut_state.initialized || !command || !response) return -1;

    memset(response, 0, sizeof(DUT_Response_t));

    if (dut_state.debug_logging) {
        /* In real implementation, would log to console/UART */
    }

    /* Send command via UART */
    char cmd_with_crlf[512];
    snprintf(cmd_with_crlf, sizeof(cmd_with_crlf), "%s\r\n", command);

    if (HAL_UART_Transmit(HAL_UART_PORT_1, (const uint8_t*)cmd_with_crlf,
                         strlen(cmd_with_crlf), timeout_ms) != 0) {
        dut_state.last_error = DUT_ERROR_TIMEOUT;
        return -1;
    }

    /* Wait for response (line-based) */
    char response_str[512];
    int bytes = HAL_UART_ReceiveLine(HAL_UART_PORT_1, response_str,
                                     sizeof(response_str) - 1, timeout_ms);

    if (bytes <= 0) {
        dut_state.last_error = DUT_ERROR_TIMEOUT;
        return -1;
    }

    response_str[bytes] = '\0';

    /* Parse response */
    if (DUT_Interface_ParseResponse(response_str, response) != 0) {
        dut_state.last_error = DUT_ERROR_UNKNOWN;
        return -1;
    }

    dut_state.last_error = response->error_code;
    memcpy(&dut_state.last_response, response, sizeof(DUT_Response_t));

    if (dut_state.debug_logging) {
        /* Log response */
    }

    return response->success ? 0 : -1;
}

/* ==================== Response Parsing ==================== */

int DUT_Interface_ParseResponse(const char* response_str,
                                DUT_Response_t* response) {
    if (!response_str || !response) return -1;

    memset(response, 0, sizeof(DUT_Response_t));

    /* Parse "OK [VALUE]" or "ERROR [CODE]" format */
    if (strncmp(response_str, "OK", 2) == 0) {
        response->success = true;
        response->error_code = DUT_SUCCESS;

        /* Extract value if present */
        const char* value_str = response_str + 2;
        while (*value_str == ' ') value_str++;

        if (*value_str != '\0') {
            response->value = strtoul(value_str, NULL, 10);
            strncpy(response->data, value_str, sizeof(response->data) - 1);
        }

        return 0;
    }

    if (strncmp(response_str, "ERROR", 5) == 0) {
        response->success = false;

        /* Extract error code if present */
        const char* error_str = response_str + 5;
        while (*error_str == ' ') error_str++;

        if (*error_str != '\0') {
            response->error_code = (DUT_ErrorCode_t)strtoul(error_str, NULL, 10);
        } else {
            response->error_code = DUT_ERROR_UNKNOWN;
        }

        return 0;
    }

    /* Unknown response format */
    return -1;
}

/* ==================== GPIO Commands ==================== */

int DUT_Interface_GPIO_Set(uint8_t port, uint8_t pin, uint8_t state,
                           uint16_t timeout_ms) {
    if (!dut_state.initialized) return -1;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "GPIO_SET %u %u %u", port, pin, state);

    DUT_Response_t response;
    return DUT_Interface_Command(cmd, &response, timeout_ms);
}

int DUT_Interface_GPIO_Get(uint8_t port, uint8_t pin, uint8_t* state,
                           uint16_t timeout_ms) {
    if (!dut_state.initialized || !state) return -1;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "GPIO_GET %u %u", port, pin);

    DUT_Response_t response;
    if (DUT_Interface_Command(cmd, &response, timeout_ms) != 0) {
        return -1;
    }

    *state = (uint8_t)response.value;
    return 0;
}

/* ==================== ADC Commands ==================== */

int DUT_Interface_ADC_Read(uint8_t channel, uint16_t* value_mv,
                           uint16_t timeout_ms) {
    if (!dut_state.initialized || !value_mv) return -1;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "ADC_READ %u", channel);

    DUT_Response_t response;
    if (DUT_Interface_Command(cmd, &response, timeout_ms) != 0) {
        return -1;
    }

    *value_mv = (uint16_t)response.value;
    return 0;
}

int DUT_Interface_ADC_Init(uint16_t timeout_ms) {
    if (!dut_state.initialized) return -1;

    DUT_Response_t response;
    return DUT_Interface_Command("ADC_INIT", &response, timeout_ms);
}

/* ==================== DAC Commands ==================== */

int DUT_Interface_DAC_Write(uint8_t channel, uint16_t value_mv,
                            uint16_t timeout_ms) {
    if (!dut_state.initialized) return -1;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "DAC_WRITE %u %u", channel, value_mv);

    DUT_Response_t response;
    return DUT_Interface_Command(cmd, &response, timeout_ms);
}

/* ==================== Status & Diagnostics ==================== */

DUT_ErrorCode_t DUT_Interface_GetLastError(void) {
    return dut_state.last_error;
}

const DUT_Response_t* DUT_Interface_GetLastResponse(void) {
    return &dut_state.last_response;
}

int DUT_Interface_GetVersion(char* version_str, size_t buffer_size,
                             uint16_t timeout_ms) {
    if (!dut_state.initialized || !version_str || buffer_size == 0) {
        return -1;
    }

    DUT_Response_t response;
    if (DUT_Interface_Command("VERSION", &response, timeout_ms) != 0) {
        return -1;
    }

    strncpy(version_str, response.data, buffer_size - 1);
    version_str[buffer_size - 1] = '\0';

    return 0;
}

/* ==================== Configuration ==================== */

void DUT_Interface_SetDefaultTimeout(uint16_t timeout_ms) {
    dut_state.default_timeout_ms = timeout_ms;
}

void DUT_Interface_SetDebugLogging(bool enable) {
    dut_state.debug_logging = enable;
}

/* ==================== Mock Test Helpers ==================== */

/**
 * @brief Mock DUT response injection for testing
 *
 * Pre-loads a response that will be returned by next command.
 * Useful for testing error paths.
 */
void DUT_Interface_Mock_InjectResponse(const char* response_str) {
    if (!response_str) return;

    /* Set up UART mock to return this response */
    HAL_UART_Mock_SetRxLineData(HAL_UART_PORT_1, response_str);
}

/**
 * @brief Reset DUT interface state (for test isolation)
 */
void DUT_Interface_Mock_Reset(void) {
    memset(&dut_state, 0, sizeof(dut_state));
    dut_state.default_timeout_ms = 1000;
}

/**
 * @brief Get last command sent to DUT
 *
 * @param buffer Buffer to receive command
 * @param buffer_size Size of buffer
 * @return Bytes copied, 0 if none
 */
uint32_t DUT_Interface_Mock_GetLastCommand(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return 0;

    /* Get from UART mock */
    int size = HAL_UART_Mock_GetTxData(HAL_UART_PORT_1, (uint8_t*)buffer,
                                       buffer_size - 1);
    if (size > 0) {
        buffer[size] = '\0';
    }
    return size;
}
