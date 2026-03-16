/**
 * @file test_hal_gpio.c
 * @brief Unit tests for GPIO HAL (using mock implementation)
 *
 * These tests run off-board and validate GPIO abstraction layer.
 */

#include "unity.h"
#include "../../hal/hal_gpio.h"
#include "../../hal/mock/hal_gpio_mock.c"  /* Include mock implementation directly */

/* ==================== Setup / Teardown ==================== */

void setUp(void) {
    HAL_GPIO_SystemInit();
}

void tearDown(void) {
    HAL_GPIO_Mock_Reset();
}

/* ==================== Test Cases ==================== */

void test_gpio_init_output_mode(void) {
    int result = HAL_GPIO_Init(HAL_GPIO_PORT_C, 9, HAL_GPIO_MODE_OUTPUT);
    TEST_ASSERT_EQUAL(0, result);

    /* Verify mode was set */
    uint16_t mode = HAL_GPIO_Mock_GetPortMode(HAL_GPIO_PORT_C);
    TEST_ASSERT_BIT_HIGH(9, mode);
}

void test_gpio_init_input_mode(void) {
    int result = HAL_GPIO_Init(HAL_GPIO_PORT_A, 5, HAL_GPIO_MODE_INPUT);
    TEST_ASSERT_EQUAL(0, result);

    /* Verify mode was cleared for input */
    uint16_t mode = HAL_GPIO_Mock_GetPortMode(HAL_GPIO_PORT_A);
    TEST_ASSERT_BIT_LOW(5, mode);
}

void test_gpio_write_pin_set(void) {
    HAL_GPIO_Init(HAL_GPIO_PORT_C, 9, HAL_GPIO_MODE_OUTPUT);

    int result = HAL_GPIO_WritePin(HAL_GPIO_PORT_C, 9, HAL_GPIO_PIN_SET);
    TEST_ASSERT_EQUAL(0, result);

    /* Verify pin state */
    int state = HAL_GPIO_ReadPin(HAL_GPIO_PORT_C, 9);
    TEST_ASSERT_EQUAL(1, state);
}

void test_gpio_write_pin_reset(void) {
    HAL_GPIO_Init(HAL_GPIO_PORT_C, 9, HAL_GPIO_MODE_OUTPUT);
    HAL_GPIO_WritePin(HAL_GPIO_PORT_C, 9, HAL_GPIO_PIN_SET);

    int result = HAL_GPIO_WritePin(HAL_GPIO_PORT_C, 9, HAL_GPIO_PIN_RESET);
    TEST_ASSERT_EQUAL(0, result);

    int state = HAL_GPIO_ReadPin(HAL_GPIO_PORT_C, 9);
    TEST_ASSERT_EQUAL(0, state);
}

void test_gpio_toggle_pin(void) {
    HAL_GPIO_Init(HAL_GPIO_PORT_C, 9, HAL_GPIO_MODE_OUTPUT);

    HAL_GPIO_WritePin(HAL_GPIO_PORT_C, 9, HAL_GPIO_PIN_SET);
    int state1 = HAL_GPIO_ReadPin(HAL_GPIO_PORT_C, 9);
    TEST_ASSERT_EQUAL(1, state1);

    HAL_GPIO_TogglePin(HAL_GPIO_PORT_C, 9);
    int state2 = HAL_GPIO_ReadPin(HAL_GPIO_PORT_C, 9);
    TEST_ASSERT_EQUAL(0, state2);

    HAL_GPIO_TogglePin(HAL_GPIO_PORT_C, 9);
    int state3 = HAL_GPIO_ReadPin(HAL_GPIO_PORT_C, 9);
    TEST_ASSERT_EQUAL(1, state3);
}

void test_gpio_multiple_pins(void) {
    /* Initialize two different pins */
    HAL_GPIO_Init(HAL_GPIO_PORT_C, 9, HAL_GPIO_MODE_OUTPUT);
    HAL_GPIO_Init(HAL_GPIO_PORT_C, 13, HAL_GPIO_MODE_OUTPUT);

    /* Set them independently */
    HAL_GPIO_WritePin(HAL_GPIO_PORT_C, 9, HAL_GPIO_PIN_SET);
    HAL_GPIO_WritePin(HAL_GPIO_PORT_C, 13, HAL_GPIO_PIN_RESET);

    /* Verify */
    TEST_ASSERT_EQUAL(1, HAL_GPIO_ReadPin(HAL_GPIO_PORT_C, 9));
    TEST_ASSERT_EQUAL(0, HAL_GPIO_ReadPin(HAL_GPIO_PORT_C, 13));
}

void test_gpio_port_read(void) {
    HAL_GPIO_Init(HAL_GPIO_PORT_C, 9, HAL_GPIO_MODE_OUTPUT);
    HAL_GPIO_Init(HAL_GPIO_PORT_C, 13, HAL_GPIO_MODE_OUTPUT);

    HAL_GPIO_WritePin(HAL_GPIO_PORT_C, 9, HAL_GPIO_PIN_SET);
    HAL_GPIO_WritePin(HAL_GPIO_PORT_C, 13, HAL_GPIO_PIN_SET);

    uint32_t port_state = HAL_GPIO_ReadPort(HAL_GPIO_PORT_C);
    TEST_ASSERT_BIT_HIGH(9, port_state);
    TEST_ASSERT_BIT_HIGH(13, port_state);
}

void test_gpio_invalid_port(void) {
    int result = HAL_GPIO_Init((HAL_GPIO_Port_t)99, 5, HAL_GPIO_MODE_OUTPUT);
    TEST_ASSERT_EQUAL(-1, result);
}

void test_gpio_invalid_pin(void) {
    int result = HAL_GPIO_Init(HAL_GPIO_PORT_C, 99, HAL_GPIO_MODE_OUTPUT);
    TEST_ASSERT_EQUAL(-1, result);
}

/* ==================== Test Runner ==================== */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_gpio_init_output_mode);
    RUN_TEST(test_gpio_init_input_mode);
    RUN_TEST(test_gpio_write_pin_set);
    RUN_TEST(test_gpio_write_pin_reset);
    RUN_TEST(test_gpio_toggle_pin);
    RUN_TEST(test_gpio_multiple_pins);
    RUN_TEST(test_gpio_port_read);
    RUN_TEST(test_gpio_invalid_port);
    RUN_TEST(test_gpio_invalid_pin);

    return UNITY_END();
}
