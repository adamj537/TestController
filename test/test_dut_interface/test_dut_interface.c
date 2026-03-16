/**
 * @file test_dut_interface.c
 * @brief Unit tests for DUT Interface (UART bridge to FW-2)
 *
 * Tests command/response protocol, GPIO/ADC/DAC operations, and error handling.
 * Uses mock UART implementation (no real hardware needed).
 */

#include "unity.h"
#include "../../dut/dut_interface.h"
#include "../../hal/hal_uart.h"
#include "../../hal/mock/hal_uart_mock.c"
#include "../../dut/dut_interface_mock.c"

/* ==================== Setup / Teardown ==================== */

void setUp(void) {
    HAL_UART_Mock_Reset();
    DUT_Interface_Mock_Reset();
    DUT_Interface_Init();
}

void tearDown(void) {
    DUT_Interface_Deinit();
    HAL_UART_Mock_Reset();
    DUT_Interface_Mock_Reset();
}

/* ==================== Response Parsing Tests ==================== */

void test_parse_ok_response_without_value(void) {
    DUT_Response_t response;
    int result = DUT_Interface_ParseResponse("OK", &response);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_TRUE(response.success);
    TEST_ASSERT_EQUAL(DUT_SUCCESS, response.error_code);
    TEST_ASSERT_EQUAL(0, response.value);
}

void test_parse_ok_response_with_value(void) {
    DUT_Response_t response;
    int result = DUT_Interface_ParseResponse("OK 1234", &response);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_TRUE(response.success);
    TEST_ASSERT_EQUAL(DUT_SUCCESS, response.error_code);
    TEST_ASSERT_EQUAL(1234, response.value);
}

void test_parse_ok_response_with_string(void) {
    DUT_Response_t response;
    int result = DUT_Interface_ParseResponse("OK FW-2-v1.0", &response);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_TRUE(response.success);
    TEST_ASSERT_EQUAL_STRING("FW-2-v1.0", response.data);
}

void test_parse_error_response_without_code(void) {
    DUT_Response_t response;
    int result = DUT_Interface_ParseResponse("ERROR", &response);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_FALSE(response.success);
    TEST_ASSERT_EQUAL(DUT_ERROR_UNKNOWN, response.error_code);
}

void test_parse_error_response_with_code(void) {
    DUT_Response_t response;
    int result = DUT_Interface_ParseResponse("ERROR 2", &response);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_FALSE(response.success);
    TEST_ASSERT_EQUAL(DUT_ERROR_INVALID_ARGUMENT, response.error_code);
}

void test_parse_invalid_response(void) {
    DUT_Response_t response;
    int result = DUT_Interface_ParseResponse("INVALID", &response);

    TEST_ASSERT_EQUAL(-1, result);
}

/* ==================== GPIO Command Tests ==================== */

void test_gpio_set_command(void) {
    /* Pre-load mock UART with expected response */
    DUT_Interface_Mock_InjectResponse("OK");

    /* Send GPIO_SET command */
    int result = DUT_Interface_GPIO_Set(2, 5, 1, 100);
    TEST_ASSERT_EQUAL(0, result);

    /* Verify command was sent */
    char cmd_buffer[256];
    uint32_t cmd_size = DUT_Interface_Mock_GetLastCommand(cmd_buffer, sizeof(cmd_buffer));
    TEST_ASSERT_GREATER_THAN(0, cmd_size);
    TEST_ASSERT_EQUAL_STRING("GPIO_SET 2 5 1\r\n", cmd_buffer);
}

void test_gpio_get_command(void) {
    /* Pre-load mock UART with GPIO state response */
    DUT_Interface_Mock_InjectResponse("OK 1");

    /* Send GPIO_GET command */
    uint8_t pin_state = 0;
    int result = DUT_Interface_GPIO_Get(2, 5, &pin_state, 100);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, pin_state);
}

void test_gpio_get_returns_zero(void) {
    DUT_Interface_Mock_InjectResponse("OK 0");

    uint8_t pin_state = 1;
    int result = DUT_Interface_GPIO_Get(2, 5, &pin_state, 100);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(0, pin_state);
}

void test_gpio_set_error(void) {
    DUT_Interface_Mock_InjectResponse("ERROR 2");

    int result = DUT_Interface_GPIO_Set(2, 99, 1, 100);
    TEST_ASSERT_EQUAL(-1, result);

    DUT_ErrorCode_t error = DUT_Interface_GetLastError();
    TEST_ASSERT_EQUAL(DUT_ERROR_INVALID_ARGUMENT, error);
}

/* ==================== ADC Command Tests ==================== */

void test_adc_read_command(void) {
    DUT_Interface_Mock_InjectResponse("OK 1650");

    uint16_t adc_value = 0;
    int result = DUT_Interface_ADC_Read(0, &adc_value, 100);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1650, adc_value);
}

void test_adc_read_max_value(void) {
    DUT_Interface_Mock_InjectResponse("OK 3300");

    uint16_t adc_value = 0;
    int result = DUT_Interface_ADC_Read(0, &adc_value, 100);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(3300, adc_value);
}

void test_adc_init_command(void) {
    DUT_Interface_Mock_InjectResponse("OK");

    int result = DUT_Interface_ADC_Init(100);
    TEST_ASSERT_EQUAL(0, result);
}

void test_adc_read_multiple_channels(void) {
    /* Read from different channels */
    DUT_Interface_Mock_InjectResponse("OK 1000");
    uint16_t ch0 = 0;
    int result0 = DUT_Interface_ADC_Read(0, &ch0, 100);
    TEST_ASSERT_EQUAL(0, result0);
    TEST_ASSERT_EQUAL(1000, ch0);

    DUT_Interface_Mock_InjectResponse("OK 2000");
    uint16_t ch1 = 0;
    int result1 = DUT_Interface_ADC_Read(1, &ch1, 100);
    TEST_ASSERT_EQUAL(0, result1);
    TEST_ASSERT_EQUAL(2000, ch1);
}

/* ==================== DAC Command Tests ==================== */

void test_dac_write_command(void) {
    DUT_Interface_Mock_InjectResponse("OK");

    int result = DUT_Interface_DAC_Write(0, 1650, 100);
    TEST_ASSERT_EQUAL(0, result);

    /* Verify command format */
    char cmd_buffer[256];
    DUT_Interface_Mock_GetLastCommand(cmd_buffer, sizeof(cmd_buffer));
    TEST_ASSERT_EQUAL_STRING("DAC_WRITE 0 1650\r\n", cmd_buffer);
}

void test_dac_write_max_voltage(void) {
    DUT_Interface_Mock_InjectResponse("OK");

    int result = DUT_Interface_DAC_Write(1, 3300, 100);
    TEST_ASSERT_EQUAL(0, result);
}

/* ==================== Status Commands ==================== */

void test_get_version(void) {
    DUT_Interface_Mock_InjectResponse("OK FW-2-v1.0.0");

    char version[256];
    int result = DUT_Interface_GetVersion(version, sizeof(version), 100);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL_STRING("FW-2-v1.0.0", version);
}

void test_is_responsive_success(void) {
    DUT_Interface_Mock_InjectResponse("OK");

    int result = DUT_Interface_IsResponsive();
    TEST_ASSERT_EQUAL(1, result);
}

void test_is_responsive_failure(void) {
    /* Don't inject response - will timeout */
    int result = DUT_Interface_IsResponsive();
    TEST_ASSERT_EQUAL(0, result);
}

/* ==================== Error Handling Tests ==================== */

void test_uninitialized_operations_fail(void) {
    DUT_Interface_Deinit();

    uint16_t adc_value = 0;
    int result = DUT_Interface_ADC_Read(0, &adc_value, 100);
    TEST_ASSERT_EQUAL(-1, result);
}

void test_timeout_error(void) {
    /* Don't inject response - will timeout */
    DUT_Response_t response;
    int result = DUT_Interface_Command("GPIO_SET 0 0 0", &response, 10);

    TEST_ASSERT_EQUAL(-1, result);
    DUT_ErrorCode_t error = DUT_Interface_GetLastError();
    TEST_ASSERT_EQUAL(DUT_ERROR_TIMEOUT, error);
}

void test_invalid_response_format(void) {
    DUT_Interface_Mock_InjectResponse("GARBAGE DATA");

    uint16_t adc_value = 0;
    int result = DUT_Interface_ADC_Read(0, &adc_value, 100);
    TEST_ASSERT_EQUAL(-1, result);
}

void test_last_response_tracking(void) {
    DUT_Interface_Mock_InjectResponse("OK 42");

    uint16_t adc_value = 0;
    DUT_Interface_ADC_Read(0, &adc_value, 100);

    const DUT_Response_t* last_response = DUT_Interface_GetLastResponse();
    TEST_ASSERT_NOT_NULL(last_response);
    TEST_ASSERT_TRUE(last_response->success);
    TEST_ASSERT_EQUAL(42, last_response->value);
}

/* ==================== Configuration Tests ==================== */

void test_set_default_timeout(void) {
    DUT_Interface_SetDefaultTimeout(500);
    /* Verify by checking that subsequent operations use it */
    /* (In real scenario, would measure timeout) */
}

void test_set_debug_logging(void) {
    DUT_Interface_SetDebugLogging(true);
    DUT_Interface_Mock_InjectResponse("OK");

    /* Command should execute normally with logging enabled */
    DUT_Response_t response;
    int result = DUT_Interface_Command("STATUS", &response, 100);
    TEST_ASSERT_EQUAL(0, result);

    DUT_Interface_SetDebugLogging(false);
}

/* ==================== Integration Tests ==================== */

void test_adc_to_dac_feedback_loop(void) {
    /* Simulate reading ADC, then setting DAC based on value */

    /* Step 1: Read ADC */
    DUT_Interface_Mock_InjectResponse("OK 1500");
    uint16_t current_adc = 0;
    int adc_result = DUT_Interface_ADC_Read(0, &current_adc, 100);
    TEST_ASSERT_EQUAL(0, adc_result);
    TEST_ASSERT_EQUAL(1500, current_adc);

    /* Step 2: Set DAC to compensate */
    DUT_Interface_Mock_InjectResponse("OK");
    uint16_t target_mv = 1800;
    int dac_result = DUT_Interface_DAC_Write(0, target_mv, 100);
    TEST_ASSERT_EQUAL(0, dac_result);
}

void test_gpio_control_sequence(void) {
    /* Simulate controlling a branch relay */

    /* Enable branch */
    DUT_Interface_Mock_InjectResponse("OK");
    int enable_result = DUT_Interface_GPIO_Set(2, 9, 1, 100);
    TEST_ASSERT_EQUAL(0, enable_result);

    /* Read state to verify */
    DUT_Interface_Mock_InjectResponse("OK 1");
    uint8_t state = 0;
    int read_result = DUT_Interface_GPIO_Get(2, 9, &state, 100);
    TEST_ASSERT_EQUAL(0, read_result);
    TEST_ASSERT_EQUAL(1, state);

    /* Disable branch */
    DUT_Interface_Mock_InjectResponse("OK");
    int disable_result = DUT_Interface_GPIO_Set(2, 9, 0, 100);
    TEST_ASSERT_EQUAL(0, disable_result);
}

void test_error_recovery(void) {
    /* Send command that fails */
    DUT_Interface_Mock_InjectResponse("ERROR 1");
    DUT_Response_t response;
    int result1 = DUT_Interface_Command("BAD_COMMAND", &response, 100);
    TEST_ASSERT_EQUAL(-1, result1);

    /* Next command should work normally */
    DUT_Interface_Mock_InjectResponse("OK 1234");
    uint16_t adc_value = 0;
    int result2 = DUT_Interface_ADC_Read(0, &adc_value, 100);
    TEST_ASSERT_EQUAL(0, result2);
    TEST_ASSERT_EQUAL(1234, adc_value);
}

/* ==================== Test Runner ==================== */

int main(void) {
    UNITY_BEGIN();

    /* Response parsing tests */
    RUN_TEST(test_parse_ok_response_without_value);
    RUN_TEST(test_parse_ok_response_with_value);
    RUN_TEST(test_parse_ok_response_with_string);
    RUN_TEST(test_parse_error_response_without_code);
    RUN_TEST(test_parse_error_response_with_code);
    RUN_TEST(test_parse_invalid_response);

    /* GPIO command tests */
    RUN_TEST(test_gpio_set_command);
    RUN_TEST(test_gpio_get_command);
    RUN_TEST(test_gpio_get_returns_zero);
    RUN_TEST(test_gpio_set_error);

    /* ADC command tests */
    RUN_TEST(test_adc_read_command);
    RUN_TEST(test_adc_read_max_value);
    RUN_TEST(test_adc_init_command);
    RUN_TEST(test_adc_read_multiple_channels);

    /* DAC command tests */
    RUN_TEST(test_dac_write_command);
    RUN_TEST(test_dac_write_max_voltage);

    /* Status command tests */
    RUN_TEST(test_get_version);
    RUN_TEST(test_is_responsive_success);
    RUN_TEST(test_is_responsive_failure);

    /* Error handling tests */
    RUN_TEST(test_uninitialized_operations_fail);
    RUN_TEST(test_timeout_error);
    RUN_TEST(test_invalid_response_format);
    RUN_TEST(test_last_response_tracking);

    /* Configuration tests */
    RUN_TEST(test_set_default_timeout);
    RUN_TEST(test_set_debug_logging);

    /* Integration tests */
    RUN_TEST(test_adc_to_dac_feedback_loop);
    RUN_TEST(test_gpio_control_sequence);
    RUN_TEST(test_error_recovery);

    return UNITY_END();
}
