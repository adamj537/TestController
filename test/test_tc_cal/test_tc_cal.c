/**
 * @file test_tc_cal.c
 * @brief Unit tests for the tc_cal calibration module
 *
 * Tests load/save roundtrip, apply functions, VDUT duty computation, JSON
 * serialization/deserialization, and the uncalibrated-sentinel behaviour.
 *
 * Build target: native (env:native in platformio.ini).
 *
 * Strategy:
 *   - Include tc_cal.c directly (single-translation-unit style) so the real
 *     implementation is under test without any modification.
 *   - Provide lightweight stubs for ESP-IDF headers (esp_log, esp_console,
 *     ESP_ERROR_CHECK) and include cJSON from the PlatformIO package.
 *   - Use the project's storage_mock.c so storage calls hit an in-RAM store.
 */

#ifndef NATIVE_BUILD
#define NATIVE_BUILD  /* guard; platformio.ini also defines it */
#endif

#ifndef ESP_PLATFORM

/* ESP-IDF stubs: esp_log.h and esp_console.h live in this directory.
 * PlatformIO adds the test directory to the include path, so tc_cal.c finds
 * them before any ESP-IDF paths. */

/* ── cJSON — path added to native build_flags in platformio.ini ──────────── */
#include "cJSON.h"
#include "/home/cbasta/.platformio/packages/framework-espidf/components/json/cJSON/cJSON.c"

/* ── storage mock ────────────────────────────────────────────────────────── */
#include "../../storage/storage.h"
#include "../../storage/storage_mock.c"

/* ── unit under test ─────────────────────────────────────────────────────── */
#include "../../src/tc_cal.h"
#include "../../src/tc_cal.c"

/* ── Unity ───────────────────────────────────────────────────────────────── */
#include "unity.h"

/* =====================================================================
 * Setup / Teardown
 * ===================================================================== */

void setUp(void)
{
    Storage_Mock_Reset();
    Storage_Init();
    tc_cal_reset();   /* in-RAM defaults; does NOT save */
}

void tearDown(void)
{
    Storage_Mock_Reset();
}

/* =====================================================================
 * Apply functions — passthrough when uncalibrated (gain=1, offset=0)
 * ===================================================================== */

void test_apply_ina_v_passthrough(void)
{
    /* Default: gain=1.0, offset=0 → corrected == raw */
    TEST_ASSERT_EQUAL_INT(1200, tc_cal_apply_ina_v(TC_CAL_INA_CH0, 1200));
    TEST_ASSERT_EQUAL_INT(3300, tc_cal_apply_ina_v(TC_CAL_INA_CH1, 3300));
}

void test_apply_ina_i_passthrough(void)
{
    TEST_ASSERT_EQUAL_INT(50,  tc_cal_apply_ina_i(TC_CAL_INA_CH0, 50));
    TEST_ASSERT_EQUAL_INT(250, tc_cal_apply_ina_i(TC_CAL_INA_CH1, 250));
}

void test_apply_adc_passthrough(void)
{
    for (int ch = 0; ch < TC_CAL_ADC_NCH; ch++)
        TEST_ASSERT_EQUAL_INT(1000, tc_cal_apply_adc(ch, 1000));
}

/* =====================================================================
 * Apply functions — with real gain / offset
 * ===================================================================== */

void test_apply_ina_v_with_calibration(void)
{
    /* gain=1.02, offset=+10 → 3300*1.02 + 10 = 3376 */
    tc_cal_set_ina_v(TC_CAL_INA_CH0, 1.02f, 10);
    int result = tc_cal_apply_ina_v(TC_CAL_INA_CH0, 3300);
    TEST_ASSERT_INT_WITHIN(2, 3376, result);   /* ±2 mV for float rounding */
}

void test_apply_ina_i_with_calibration(void)
{
    /* gain=0.98, offset=-2 → 100*0.98 - 2 = 96 */
    tc_cal_set_ina_i(TC_CAL_INA_CH1, 0.98f, -2);
    int result = tc_cal_apply_ina_i(TC_CAL_INA_CH1, 100);
    TEST_ASSERT_INT_WITHIN(1, 96, result);
}

void test_apply_adc_with_calibration(void)
{
    /* ch3: gain=1.05, offset=-5 → 2000*1.05 - 5 = 2095 */
    tc_cal_set_adc(3, 1.05f, -5);
    int result = tc_cal_apply_adc(3, 2000);
    TEST_ASSERT_INT_WITHIN(2, 2095, result);
}

void test_apply_does_not_bleed_channels(void)
{
    /* Only ch0 calibrated; ch1 must remain passthrough */
    tc_cal_set_ina_v(TC_CAL_INA_CH0, 2.0f, 0);
    TEST_ASSERT_EQUAL_INT(500, tc_cal_apply_ina_v(TC_CAL_INA_CH0, 250));
    TEST_ASSERT_EQUAL_INT(250, tc_cal_apply_ina_v(TC_CAL_INA_CH1, 250));
}

/* =====================================================================
 * VDUT duty computation
 * ===================================================================== */

void test_vdut_duty_uncalibrated_returns_minus1(void)
{
    /* Default: slope=0 (uncalibrated sentinel) */
    TEST_ASSERT_EQUAL_INT(-1, tc_cal_vdut_duty_for_mv(3300));
}

void test_vdut_duty_typical_values(void)
{
    /* slope=-87 mV/pct, intercept=11200 mV
     * duty = (3300 - 11200) / -87 = -7900 / -87 ≈ 90.8 → 90 or 91 */
    tc_cal_set_vdut(-87, 11200);
    int duty = tc_cal_vdut_duty_for_mv(3300);
    TEST_ASSERT_INT_WITHIN(1, 91, duty);   /* allow ±1 for integer truncation */
}

void test_vdut_duty_clamped_low(void)
{
    tc_cal_set_vdut(-87, 11200);
    /* duty=1 at V=11113 mV; higher V gives duty<1 → clamp to 1.
     * 20000 mV: (20000-11200)/-87 = -101 → clamped to 1 */
    int duty = tc_cal_vdut_duty_for_mv(20000);
    TEST_ASSERT_EQUAL_INT(1, duty);
}

void test_vdut_duty_clamped_high(void)
{
    tc_cal_set_vdut(-87, 11200);
    /* Very low voltage → very high duty; must clamp to 99 */
    int duty = tc_cal_vdut_duty_for_mv(100);
    TEST_ASSERT_EQUAL_INT(99, duty);
}

/* =====================================================================
 * get / set round-trip (in-RAM only)
 * ===================================================================== */

void test_vdut_get_set_roundtrip(void)
{
    tc_cal_set_vdut(-55, 9800);
    int slope, intercept;
    tc_cal_get_vdut(&slope, &intercept);
    TEST_ASSERT_EQUAL_INT(-55, slope);
    TEST_ASSERT_EQUAL_INT(9800, intercept);
}

void test_ina_v_get_set_roundtrip(void)
{
    tc_cal_set_ina_v(TC_CAL_INA_CH1, 1.03f, 15);
    float gain;
    int offset;
    tc_cal_get_ina_v(TC_CAL_INA_CH1, &gain, &offset);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.03f, gain);
    TEST_ASSERT_EQUAL_INT(15, offset);
}

void test_adc_get_set_roundtrip(void)
{
    tc_cal_set_adc(7, 0.95f, -3);
    float gain;
    int offset;
    tc_cal_get_adc(7, &gain, &offset);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.95f, gain);
    TEST_ASSERT_EQUAL_INT(-3, offset);
}

/* =====================================================================
 * Persist & reload (storage roundtrip)
 * ===================================================================== */

void test_save_load_vdut(void)
{
    tc_cal_set_vdut(-87, 11200);
    int rc = tc_cal_save();
    TEST_ASSERT_EQUAL_INT(0, rc);

    tc_cal_reset();   /* wipe in-RAM */
    int slope, intercept;
    tc_cal_get_vdut(&slope, &intercept);
    TEST_ASSERT_EQUAL_INT(0, slope);   /* confirm reset */

    rc = tc_cal_load();
    TEST_ASSERT_EQUAL_INT(0, rc);

    tc_cal_get_vdut(&slope, &intercept);
    TEST_ASSERT_EQUAL_INT(-87, slope);
    TEST_ASSERT_EQUAL_INT(11200, intercept);
}

void test_save_load_ina_calibration(void)
{
    tc_cal_set_ina_v(TC_CAL_INA_CH0, 1.02f, 8);
    tc_cal_set_ina_i(TC_CAL_INA_CH0, 0.97f, -3);
    tc_cal_set_ina_v(TC_CAL_INA_CH1, 1.01f, 5);
    tc_cal_set_ina_i(TC_CAL_INA_CH1, 0.99f, -1);
    TEST_ASSERT_EQUAL_INT(0, tc_cal_save());

    tc_cal_reset();
    TEST_ASSERT_EQUAL_INT(0, tc_cal_load());

    float gain; int offset;

    tc_cal_get_ina_v(TC_CAL_INA_CH0, &gain, &offset);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.02f, gain);
    TEST_ASSERT_EQUAL_INT(8, offset);

    tc_cal_get_ina_i(TC_CAL_INA_CH0, &gain, &offset);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.97f, gain);
    TEST_ASSERT_EQUAL_INT(-3, offset);

    tc_cal_get_ina_v(TC_CAL_INA_CH1, &gain, &offset);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.01f, gain);
    TEST_ASSERT_EQUAL_INT(5, offset);
}

void test_save_load_adc_calibration(void)
{
    for (int ch = 0; ch < TC_CAL_ADC_NCH; ch++)
        tc_cal_set_adc(ch, 1.0f + (float)ch * 0.01f, ch * 2);

    TEST_ASSERT_EQUAL_INT(0, tc_cal_save());
    tc_cal_reset();
    TEST_ASSERT_EQUAL_INT(0, tc_cal_load());

    for (int ch = 0; ch < TC_CAL_ADC_NCH; ch++) {
        float gain; int offset;
        tc_cal_get_adc(ch, &gain, &offset);
        TEST_ASSERT_FLOAT_WITHIN(0.002f, 1.0f + (float)ch * 0.01f, gain);
        TEST_ASSERT_EQUAL_INT(ch * 2, offset);
    }
}

void test_load_from_empty_storage_gives_defaults(void)
{
    /* Nothing saved — load should succeed and give passthrough defaults */
    int rc = tc_cal_load();
    TEST_ASSERT_EQUAL_INT(0, rc);

    int slope, intercept;
    tc_cal_get_vdut(&slope, &intercept);
    TEST_ASSERT_EQUAL_INT(0, slope);   /* uncalibrated sentinel */

    float gain; int offset;
    tc_cal_get_ina_v(TC_CAL_INA_CH0, &gain, &offset);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, gain);
    TEST_ASSERT_EQUAL_INT(0, offset);
}

void test_reset_clears_calibration(void)
{
    tc_cal_set_vdut(-87, 11200);
    tc_cal_set_ina_v(TC_CAL_INA_CH0, 1.05f, 20);
    tc_cal_reset();

    int slope, intercept;
    tc_cal_get_vdut(&slope, &intercept);
    TEST_ASSERT_EQUAL_INT(0, slope);

    float gain; int offset;
    tc_cal_get_ina_v(TC_CAL_INA_CH0, &gain, &offset);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, gain);
    TEST_ASSERT_EQUAL_INT(0, offset);
}

/* =====================================================================
 * CalProfileVersion logic (VDUT slope sentinel)
 * ===================================================================== */

void test_vdut_calibrated_flag(void)
{
    /* Uncalibrated: slope == 0 */
    int slope, intercept;
    tc_cal_get_vdut(&slope, &intercept);
    TEST_ASSERT_EQUAL_INT(0, slope);

    /* Calibrated: slope != 0 */
    tc_cal_set_vdut(-87, 11200);
    tc_cal_get_vdut(&slope, &intercept);
    TEST_ASSERT_NOT_EQUAL(0, slope);
}

/* =====================================================================
 * Test Runner
 * ===================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Passthrough (default) apply */
    RUN_TEST(test_apply_ina_v_passthrough);
    RUN_TEST(test_apply_ina_i_passthrough);
    RUN_TEST(test_apply_adc_passthrough);

    /* Calibrated apply */
    RUN_TEST(test_apply_ina_v_with_calibration);
    RUN_TEST(test_apply_ina_i_with_calibration);
    RUN_TEST(test_apply_adc_with_calibration);
    RUN_TEST(test_apply_does_not_bleed_channels);

    /* VDUT duty computation */
    RUN_TEST(test_vdut_duty_uncalibrated_returns_minus1);
    RUN_TEST(test_vdut_duty_typical_values);
    RUN_TEST(test_vdut_duty_clamped_low);
    RUN_TEST(test_vdut_duty_clamped_high);

    /* In-RAM get/set round-trip */
    RUN_TEST(test_vdut_get_set_roundtrip);
    RUN_TEST(test_ina_v_get_set_roundtrip);
    RUN_TEST(test_adc_get_set_roundtrip);

    /* Storage persist / reload */
    RUN_TEST(test_save_load_vdut);
    RUN_TEST(test_save_load_ina_calibration);
    RUN_TEST(test_save_load_adc_calibration);
    RUN_TEST(test_load_from_empty_storage_gives_defaults);
    RUN_TEST(test_reset_clears_calibration);

    /* CalProfileVersion sentinel */
    RUN_TEST(test_vdut_calibrated_flag);

    return UNITY_END();
}

#endif /* !ESP_PLATFORM */
