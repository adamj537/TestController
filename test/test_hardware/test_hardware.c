/**
 * @file test_hardware.c
 * @brief Unit tests for Hardware module (using mock HAL)
 *
 * Validates Hardware interface layer without real hardware.
 * Tests voltage control, current measurement, branch selection, and LEDs.
 */

#include "unity.h"
#include "../../app/Hardware.h"
#include "../../hal/hal_gpio.h"
#include "../../hal/hal_adc.h"
#include "../../hal/hal_dac.h"
#include "../../hal/hal_system.h"

/* Include mock implementations directly for testing */
#include "../../hal/mock/hal_gpio_mock.c"
#include "../../hal/mock/hal_adc_mock.c"
#include "../../hal/mock/hal_dac_mock.c"
#include "../../hal/mock/hal_system_mock.c"

/* Include BSP and Hardware implementations (uses HAL — mocks provide the HAL) */
#include "../../bsp/bsp_g3_tc.c"
#include "../../app/Hardware.cpp"

/* Forward declaration for test-only reset (defined in Hardware.cpp under NATIVE_BUILD) */
extern void Hardware_Mock_Reset(void);

/* ==================== Setup / Teardown ==================== */

void setUp(void) {
    Hardware_Mock_Reset();
    HAL_GPIO_SystemInit();
    HAL_System_Init();
    HAL_ADC_Init(HAL_ADC_UNIT_INTERNAL, HAL_ADC_RESOLUTION_12BIT);
    HAL_DAC_Init(HAL_DAC_RESOLUTION_12BIT);
}

void tearDown(void) {
    Hardware_Mock_Reset();
    HAL_GPIO_Mock_Reset();
    HAL_ADC_Mock_Reset();
    HAL_DAC_Mock_Reset();
    HAL_System_Mock_Reset();
}

/* ==================== Test Cases ==================== */

void test_hardware_begin_initializes(void) {
    int result = begin();
    TEST_ASSERT_EQUAL(0, result);
}

void test_hardware_begin_not_already_initialized(void) {
    int result1 = begin();
    TEST_ASSERT_EQUAL(0, result1);

    /* Second call should return 0 (already initialized) */
    int result2 = begin();
    TEST_ASSERT_EQUAL(0, result2);
}

void test_hardware_adjust_voltage_sets_dac(void) {
    begin();

    int result = adjustVoltage(1650);  /* Set to 1.65V (half of 3.3V) */
    TEST_ASSERT_EQUAL(0, result);

    /* Verify DAC was written */
    int32_t dac_value = HAL_DAC_Mock_GetChannelValue(0);  /* Channel 0 = voltage */
    TEST_ASSERT_NOT_EQUAL(-1, dac_value);
}

void test_hardware_adjust_voltage_rejects_out_of_range(void) {
    begin();

    int result1 = adjustVoltage(-100);    /* Negative */
    TEST_ASSERT_EQUAL(-1, result1);

    int result2 = adjustVoltage(5000);    /* > 3.3V */
    TEST_ASSERT_EQUAL(-1, result2);
}

void test_hardware_read_voltage_from_adc(void) {
    begin();

    /* Set mock ADC to return a known value */
    HAL_ADC_Mock_SetChannelValue(HAL_ADC_UNIT_INTERNAL, 0, 1650);  /* Channel 0 = voltage feedback */

    int32_t voltage = readVoltage();
    TEST_ASSERT_EQUAL(1650, voltage);
}

void test_hardware_read_current_from_adc(void) {
    begin();

    /* Set mock ADC to return 500mA */
    HAL_ADC_Mock_SetChannelValue(HAL_ADC_UNIT_INTERNAL, 1, 500);  /* Channel 1 = current sense */

    int32_t current = readCurrent();
    TEST_ASSERT_EQUAL(500, current);
}

void test_hardware_set_stimulus_signal(void) {
    begin();

    int result = setStimulusSignal(2000);  /* Set 2V stimulus */
    TEST_ASSERT_EQUAL(0, result);

    int32_t dac_value = HAL_DAC_Mock_GetChannelValue(1);  /* Channel 1 = stimulus */
    TEST_ASSERT_NOT_EQUAL(-1, dac_value);
}

void test_hardware_read_response_signal(void) {
    begin();

    /* Set mock ADC to return DUT response */
    HAL_ADC_Mock_SetChannelValue(HAL_ADC_UNIT_INTERNAL, 2, 1200);  /* Channel 2 = response */

    int32_t response = readResponseSignal();
    TEST_ASSERT_EQUAL(1200, response);
}

void test_hardware_select_branch_1(void) {
    begin();

    int result = selectBranch(1);
    TEST_ASSERT_EQUAL(0, result);

    /* Verify branch 1 GPIO is set */
    int pin_state = HAL_GPIO_ReadPin(HAL_GPIO_PORT_C, 9);  /* Branch 1 = PC9 */
    TEST_ASSERT_EQUAL(1, pin_state);
}

void test_hardware_select_branch_2(void) {
    begin();

    int result = selectBranch(2);
    TEST_ASSERT_EQUAL(0, result);

    /* Verify branch 2 GPIO is set */
    int pin_state = HAL_GPIO_ReadPin(HAL_GPIO_PORT_B, 11);  /* Branch 2 = PB11 */
    TEST_ASSERT_EQUAL(1, pin_state);
}

void test_hardware_select_branch_3(void) {
    begin();

    int result = selectBranch(3);
    TEST_ASSERT_EQUAL(0, result);

    /* Verify branch 3 GPIO is set */
    int pin_state = HAL_GPIO_ReadPin(HAL_GPIO_PORT_D, 15);  /* Branch 3 = PD15 */
    TEST_ASSERT_EQUAL(1, pin_state);
}

void test_hardware_select_branch_4(void) {
    begin();

    int result = selectBranch(4);
    TEST_ASSERT_EQUAL(0, result);

    /* Verify branch 4 GPIO is set */
    int pin_state = HAL_GPIO_ReadPin(HAL_GPIO_PORT_C, 13);  /* Branch 4 = PC13 */
    TEST_ASSERT_EQUAL(1, pin_state);
}

void test_hardware_select_branch_switches_branches(void) {
    begin();

    /* Select branch 1 */
    selectBranch(1);
    TEST_ASSERT_EQUAL(1, HAL_GPIO_ReadPin(HAL_GPIO_PORT_C, 9));

    /* Switch to branch 2 (should disable branch 1) */
    selectBranch(2);
    TEST_ASSERT_EQUAL(0, HAL_GPIO_ReadPin(HAL_GPIO_PORT_C, 9));
    TEST_ASSERT_EQUAL(1, HAL_GPIO_ReadPin(HAL_GPIO_PORT_B, 11));
}

void test_hardware_deselect_all_branches(void) {
    begin();

    /* Enable all branches */
    selectBranch(1);
    selectBranch(2);
    selectBranch(3);
    selectBranch(4);

    /* Deselect all */
    int result = deselectAllBranches();
    TEST_ASSERT_EQUAL(0, result);

    /* Verify all are disabled */
    TEST_ASSERT_EQUAL(0, HAL_GPIO_ReadPin(HAL_GPIO_PORT_C, 9));
    TEST_ASSERT_EQUAL(0, HAL_GPIO_ReadPin(HAL_GPIO_PORT_B, 11));
    TEST_ASSERT_EQUAL(0, HAL_GPIO_ReadPin(HAL_GPIO_PORT_D, 15));
    TEST_ASSERT_EQUAL(0, HAL_GPIO_ReadPin(HAL_GPIO_PORT_C, 13));
}

void test_hardware_select_branch_invalid_number(void) {
    begin();

    int result1 = selectBranch(0);      /* Too low */
    TEST_ASSERT_EQUAL(-1, result1);

    int result2 = selectBranch(5);      /* Too high */
    TEST_ASSERT_EQUAL(-1, result2);
}

void test_hardware_heartbeat_toggles_led(void) {
    begin();

    /* Status LED should start off */
    TEST_ASSERT_EQUAL(0, HAL_GPIO_ReadPin(HAL_GPIO_PORT_E, 5));

    /* Toggle on */
    heartbeat();
    TEST_ASSERT_EQUAL(1, HAL_GPIO_ReadPin(HAL_GPIO_PORT_E, 5));

    /* Toggle off */
    heartbeat();
    TEST_ASSERT_EQUAL(0, HAL_GPIO_ReadPin(HAL_GPIO_PORT_E, 5));
}

void test_hardware_set_error_led_on(void) {
    begin();

    int result = setErrorLed(true);
    TEST_ASSERT_EQUAL(0, result);

    int state = HAL_GPIO_ReadPin(HAL_GPIO_PORT_C, 7);
    TEST_ASSERT_EQUAL(1, state);
}

void test_hardware_set_error_led_off(void) {
    begin();

    setErrorLed(true);
    int result = setErrorLed(false);
    TEST_ASSERT_EQUAL(0, result);

    int state = HAL_GPIO_ReadPin(HAL_GPIO_PORT_C, 7);
    TEST_ASSERT_EQUAL(0, state);
}

void test_hardware_operations_fail_before_init(void) {
    /* Don't call begin() */

    TEST_ASSERT_EQUAL(-1, adjustVoltage(1650));
    TEST_ASSERT_EQUAL(-1, readVoltage());
    TEST_ASSERT_EQUAL(-1, readCurrent());
    TEST_ASSERT_EQUAL(-1, setStimulusSignal(1000));
    TEST_ASSERT_EQUAL(-1, readResponseSignal());
    TEST_ASSERT_EQUAL(-1, selectBranch(1));
    TEST_ASSERT_EQUAL(-1, deselectAllBranches());
    TEST_ASSERT_EQUAL(-1, heartbeat());
    TEST_ASSERT_EQUAL(-1, setErrorLed(true));
}

/* ==================== Test Runner ==================== */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_hardware_begin_initializes);
    RUN_TEST(test_hardware_begin_not_already_initialized);
    RUN_TEST(test_hardware_adjust_voltage_sets_dac);
    RUN_TEST(test_hardware_adjust_voltage_rejects_out_of_range);
    RUN_TEST(test_hardware_read_voltage_from_adc);
    RUN_TEST(test_hardware_read_current_from_adc);
    RUN_TEST(test_hardware_set_stimulus_signal);
    RUN_TEST(test_hardware_read_response_signal);
    RUN_TEST(test_hardware_select_branch_1);
    RUN_TEST(test_hardware_select_branch_2);
    RUN_TEST(test_hardware_select_branch_3);
    RUN_TEST(test_hardware_select_branch_4);
    RUN_TEST(test_hardware_select_branch_switches_branches);
    RUN_TEST(test_hardware_deselect_all_branches);
    RUN_TEST(test_hardware_select_branch_invalid_number);
    RUN_TEST(test_hardware_heartbeat_toggles_led);
    RUN_TEST(test_hardware_set_error_led_on);
    RUN_TEST(test_hardware_set_error_led_off);
    RUN_TEST(test_hardware_operations_fail_before_init);

    return UNITY_END();
}
