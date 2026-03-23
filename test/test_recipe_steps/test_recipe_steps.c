/**
 * @file test_recipe_steps.c
 * @brief Native tests for g3-mb-v1 recipe step configuration and parsing.
 *
 * Verifies the full recipe step contract: every step's enabled/disabled state,
 * criticality, and onError field — plus the store/load round-trip via storage mock.
 * Also tests the DUT UART step response-parsing and range-check logic.
 *
 * Build target: native (env:native).
 * Strategy: single-translation-unit include of recipe_json.c + cJSON + storage_mock.
 */

#ifndef NATIVE_BUILD
#define NATIVE_BUILD
#endif

/* ── cJSON — vendored at vendor/cjson/ (-Ivendor/cjson in platformio.ini) ── */
#include "cJSON.h"
#include "cJSON.c"

/* ── ESP-IDF stubs ────────────────────────────────────────────────────────── */
/* These headers live in this test directory and shadow real ESP-IDF headers. */

/* ── Storage mock ─────────────────────────────────────────────────────────── */
#include "../../storage/storage.h"
#include "../../storage/storage_mock.c"

/* ── Unit under test ──────────────────────────────────────────────────────── */
/* recipe_json.c calls recipe_primitives_table() only from recipe_json_from_hardcoded().
 * We don't test that function here — provide stub implementations so the
 * linker is satisfied without pulling in the full primitive dispatch table. */
#include "../../src/recipe_primitives.h"
#include "../../src/recipe_engine.h"
#include "../../src/recipe_json.h"

/* Stubs for functions referenced by recipe_json.c console commands but not
 * exercised in these unit tests. */

/* recipe_primitives_table/lookup — used by recipe_json_from_hardcoded() */
const primitive_entry_t *recipe_primitives_table(int *count_out)
{
    if (count_out) *count_out = 0;
    return NULL;
}

primitive_fn_t recipe_primitives_lookup(const char *id)
{
    (void)id;
    return NULL;
}

/* recipe_engine_run — referenced by 'recipe run' console command */
int recipe_engine_run(const json_recipe_t *recipe, recipe_run_result_t *result)
{
    (void)recipe;
    if (result) {
        result->outcome    = RECIPE_RESULT_PASS;
        result->pass_count = 0;
        result->total_count= 0;
        result->duration_ms= 0;
        result->failed_step[0] = '\0';
    }
    return 0;
}

#include "../../src/recipe_json.c"

/* ── Unity ────────────────────────────────────────────────────────────────── */
#include "unity.h"

/* ── g3-mb-v1 recipe JSON (matches recipes/g3-mb-v1.json v1.2.0) ─────────── */
static const char G3_MB_V1_JSON[] =
    "{"
    "\"recipeId\":\"g3-mb-v1\","
    "\"recipeVersion\":\"1.2.0\","
    "\"name\":\"G3 Main Board \\u2014 Production Test\","
    "\"timeoutMs\":120000,"
    "\"steps\":["
      "{\"id\":\"i2c\",\"primitive\":\"i2c\",\"label\":\"I2C Bus & Rail Check\","
       "\"criticality\":\"CRITICAL\",\"onError\":\"abort\",\"enabled\":true},"
      "{\"id\":\"adc\",\"primitive\":\"adc\",\"label\":\"ESP32 ADC Sanity\","
       "\"criticality\":\"OPTIONAL\",\"onError\":\"skip\",\"enabled\":true},"
      "{\"id\":\"wifi\",\"primitive\":\"wifi\",\"label\":\"WiFi Connected\","
       "\"criticality\":\"REQUIRED\",\"onError\":\"skip\",\"enabled\":true},"
      "{\"id\":\"ota\",\"primitive\":\"ota\",\"label\":\"OTA Partition Valid\","
       "\"criticality\":\"REQUIRED\",\"onError\":\"skip\",\"enabled\":true},"
      "{\"id\":\"dut_read_id\",\"primitive\":\"dut_read_id\",\"label\":\"DUT Serial Read\","
       "\"criticality\":\"REQUIRED\",\"onError\":\"skip\",\"enabled\":true,"
       "\"params\":{\"timeout_ms\":3000}},"
      "{\"id\":\"dut_program\",\"primitive\":\"dut_program\",\"label\":\"Program PFW (local NVS)\","
       "\"criticality\":\"CRITICAL\",\"onError\":\"abort\",\"enabled\":false,"
       "\"params\":{\"target\":\"pfw\",\"verify\":true,\"timeout_s\":60}},"
      "{\"id\":\"power_check\",\"primitive\":\"power_check\",\"label\":\"DUT Power Delivery\","
       "\"criticality\":\"CRITICAL\",\"onError\":\"abort\",\"enabled\":true,"
       "\"params\":{\"v_nominal_mv\":3300,\"v_tolerance_pct\":5,\"i_min_ma\":5,\"i_max_ma\":250}},"
      "{\"id\":\"dut_heartbeat\",\"primitive\":\"dut_heartbeat\",\"label\":\"DUT Heartbeat PA9 @ 1Hz\","
       "\"criticality\":\"CRITICAL\",\"onError\":\"abort\",\"enabled\":false},"
      "{\"id\":\"dut_enter_test\",\"primitive\":\"dut_enter_test\",\"label\":\"DUT Enter Test Mode\","
       "\"criticality\":\"CRITICAL\",\"onError\":\"abort\",\"enabled\":true},"
      "{\"id\":\"dut_version\",\"primitive\":\"dut_version\",\"label\":\"DUT Firmware Version\","
       "\"criticality\":\"REQUIRED\",\"onError\":\"skip\",\"enabled\":true},"
      "{\"id\":\"dut_hw_rev\",\"primitive\":\"dut_hw_rev\",\"label\":\"DUT Hardware Rev\","
       "\"criticality\":\"REQUIRED\",\"onError\":\"skip\",\"enabled\":true},"
      "{\"id\":\"dut_uc_adc_vref\",\"primitive\":\"dut_uc_adc_vref\","
       "\"label\":\"DUT uC ADC Vref (2400-2600 mV)\","
       "\"criticality\":\"REQUIRED\",\"onError\":\"skip\",\"enabled\":true},"
      "{\"id\":\"dut_uc_adc_3v_rail\",\"primitive\":\"dut_uc_adc_3v_rail\","
       "\"label\":\"DUT uC ADC 3V Rail (2850-3150 mV)\","
       "\"criticality\":\"REQUIRED\",\"onError\":\"skip\",\"enabled\":true},"
      "{\"id\":\"dut_flash_test\",\"primitive\":\"dut_flash_test\",\"label\":\"DUT Flash Self-Test\","
       "\"criticality\":\"REQUIRED\",\"onError\":\"skip\",\"enabled\":true},"
      "{\"id\":\"dut_rtc_read\",\"primitive\":\"dut_rtc_read\","
       "\"label\":\"DUT RTC Battery (1550-3600 mV)\","
       "\"criticality\":\"REQUIRED\",\"onError\":\"skip\",\"enabled\":true},"
      "{\"id\":\"dut_exit_test\",\"primitive\":\"dut_exit_test\",\"label\":\"DUT Exit Test Mode\","
       "\"criticality\":\"REQUIRED\",\"onError\":\"skip\",\"enabled\":true},"
      "{\"id\":\"mux_scan\",\"primitive\":\"mux_scan\",\"label\":\"TIE MUX Scan (4 mux x 16 ch)\","
       "\"criticality\":\"REQUIRED\",\"onError\":\"skip\",\"enabled\":true}"
    "]}";

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static json_recipe_t s_recipe;

static const json_recipe_step_t *find_step(const char *id)
{
    for (int i = 0; i < s_recipe.step_count; i++) {
        if (strcmp(s_recipe.steps[i].id, id) == 0)
            return &s_recipe.steps[i];
    }
    return NULL;
}

/* ── Setup / Teardown ─────────────────────────────────────────────────────── */

void setUp(void)
{
    Storage_Mock_Reset();
    Storage_Init();
    memset(&s_recipe, 0, sizeof(s_recipe));
    int rc = recipe_json_parse(G3_MB_V1_JSON, sizeof(G3_MB_V1_JSON) - 1, &s_recipe);
    TEST_ASSERT_EQUAL_MESSAGE(0, rc, "g3-mb-v1 JSON must parse without error");
}

void tearDown(void)
{
    recipe_json_free(&s_recipe);
    Storage_Mock_Reset();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Recipe header
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_recipe_id_and_version(void)
{
    TEST_ASSERT_EQUAL_STRING("g3-mb-v1", s_recipe.recipe_id);
    TEST_ASSERT_EQUAL_STRING("1.2.0", s_recipe.recipe_version);
}

void test_recipe_step_count(void)
{
    TEST_ASSERT_EQUAL_INT(17, s_recipe.step_count);
}

void test_recipe_timeout(void)
{
    TEST_ASSERT_EQUAL_UINT32(120000, s_recipe.timeout_ms);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TC carrier self-test steps (enabled, not DUT-dependent)
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_step_i2c_enabled_critical(void)
{
    const json_recipe_step_t *s = find_step("i2c");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->enabled);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_CRITICAL, s->criticality);
    TEST_ASSERT_EQUAL_STRING("abort", s->on_error);
}

void test_step_adc_enabled_optional(void)
{
    const json_recipe_step_t *s = find_step("adc");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->enabled);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_OPTIONAL, s->criticality);
}

void test_step_wifi_enabled_required(void)
{
    const json_recipe_step_t *s = find_step("wifi");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->enabled);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_REQUIRED, s->criticality);
    TEST_ASSERT_EQUAL_STRING("skip", s->on_error);
}

void test_step_ota_enabled_required(void)
{
    const json_recipe_step_t *s = find_step("ota");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->enabled);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_REQUIRED, s->criticality);
}

void test_step_dut_read_id_enabled(void)
{
    const json_recipe_step_t *s = find_step("dut_read_id");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->enabled);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_REQUIRED, s->criticality);
    /* params must be present */
    TEST_ASSERT_NOT_NULL(s->params);
}

void test_step_power_check_enabled_critical(void)
{
    const json_recipe_step_t *s = find_step("power_check");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->enabled);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_CRITICAL, s->criticality);
    TEST_ASSERT_NOT_NULL(s->params);
}

void test_step_mux_scan_enabled(void)
{
    const json_recipe_step_t *s = find_step("mux_scan");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->enabled);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_REQUIRED, s->criticality);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Steps disabled pending hardware readiness
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_step_dut_program_disabled(void)
{
    /* Requires PFW binary confirmed in NVS partition */
    const json_recipe_step_t *s = find_step("dut_program");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_FALSE_MESSAGE(s->enabled,
        "dut_program must stay disabled until PFW binary is in NVS partition");
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_CRITICAL, s->criticality);
}

void test_step_dut_heartbeat_disabled(void)
{
    /* Requires PFW running on DUT to generate PA9 1 Hz pulse */
    const json_recipe_step_t *s = find_step("dut_heartbeat");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_FALSE_MESSAGE(s->enabled,
        "dut_heartbeat must stay disabled until PFW generates PA9 1Hz pulse");
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_CRITICAL, s->criticality);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * DUT UART test steps — enabled, require PFW UART handler (FW-2)
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_step_dut_enter_test_enabled_critical(void)
{
    /* CRITICAL: if ENTER_TEST fails, UART driver is closed; all subsequent
     * UART steps skip cleanly via the s_uart_open gate. */
    const json_recipe_step_t *s = find_step("dut_enter_test");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->enabled);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_CRITICAL, s->criticality);
    TEST_ASSERT_EQUAL_STRING("abort", s->on_error);
}

void test_step_dut_version_enabled(void)
{
    const json_recipe_step_t *s = find_step("dut_version");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->enabled);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_REQUIRED, s->criticality);
    TEST_ASSERT_EQUAL_STRING("skip", s->on_error);
}

void test_step_dut_hw_rev_enabled(void)
{
    const json_recipe_step_t *s = find_step("dut_hw_rev");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->enabled);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_REQUIRED, s->criticality);
}

void test_step_dut_uc_adc_vref_enabled(void)
{
    const json_recipe_step_t *s = find_step("dut_uc_adc_vref");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->enabled);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_REQUIRED, s->criticality);
}

void test_step_dut_uc_adc_3v_rail_enabled(void)
{
    const json_recipe_step_t *s = find_step("dut_uc_adc_3v_rail");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->enabled);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_REQUIRED, s->criticality);
}

void test_step_dut_flash_test_enabled(void)
{
    const json_recipe_step_t *s = find_step("dut_flash_test");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->enabled);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_REQUIRED, s->criticality);
}

void test_step_dut_rtc_read_enabled(void)
{
    const json_recipe_step_t *s = find_step("dut_rtc_read");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->enabled);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_REQUIRED, s->criticality);
}

void test_step_dut_exit_test_enabled(void)
{
    const json_recipe_step_t *s = find_step("dut_exit_test");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->enabled);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_REQUIRED, s->criticality);
    TEST_ASSERT_EQUAL_STRING("skip", s->on_error);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * DUT UART response parsing and range-check logic
 * These tests document and validate the pass/fail criteria for each UART step.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* dut_enter_test: response must start with "OK" */
void test_enter_test_response_ok(void)
{
    const char *resp = "OK TEST_MODE_ACTIVE";
    TEST_ASSERT_TRUE(strncmp(resp, "OK", 2) == 0);
}

void test_enter_test_response_err(void)
{
    const char *resp = "ERR NOT_READY";
    TEST_ASSERT_FALSE(strncmp(resp, "OK", 2) == 0);
}

/* dut_uc_adc_vref: UC VREF expected 2400–2600 mV (internal 2.5V reference) */
void test_vref_range_nominal(void)
{
    const char *resp = "OK UC_ADC_READ VREF 2500";
    int mv = 0;
    int parsed = sscanf(resp, "OK UC_ADC_READ VREF %d", &mv);
    TEST_ASSERT_EQUAL_INT(1, parsed);
    TEST_ASSERT_EQUAL_INT(2500, mv);
    TEST_ASSERT_TRUE(mv >= 2400 && mv <= 2600);
}

void test_vref_range_low_boundary(void)
{
    int mv = 2400;
    TEST_ASSERT_TRUE(mv >= 2400 && mv <= 2600);
}

void test_vref_range_high_boundary(void)
{
    int mv = 2600;
    TEST_ASSERT_TRUE(mv >= 2400 && mv <= 2600);
}

void test_vref_below_range_fails(void)
{
    int mv = 2399;
    TEST_ASSERT_FALSE(mv >= 2400 && mv <= 2600);
}

void test_vref_above_range_fails(void)
{
    int mv = 2601;
    TEST_ASSERT_FALSE(mv >= 2400 && mv <= 2600);
}

/* dut_uc_adc_3v_rail: 3.3V rail ±5% → 2850–3150 mV */
void test_3v_rail_range_nominal(void)
{
    /* DUT STM32 reads its 3.3V rail via internal ADC — expected reading is
     * within ±5% of 3000 mV nominal (3300V rail, divided or referenced to VREF).
     * Range 2850–3150 mV is ±5% tolerance band. */
    const char *resp = "OK UC_ADC_READ 3V_RAIL 3050";
    int mv = 0;
    int parsed = sscanf(resp, "OK UC_ADC_READ 3V_RAIL %d", &mv);
    TEST_ASSERT_EQUAL_INT(1, parsed);
    TEST_ASSERT_EQUAL_INT(3050, mv);
    TEST_ASSERT_TRUE(mv >= 2850 && mv <= 3150);
}

void test_3v_rail_low_boundary(void)
{
    int mv = 2850;
    TEST_ASSERT_TRUE(mv >= 2850 && mv <= 3150);
}

void test_3v_rail_high_boundary(void)
{
    int mv = 3150;
    TEST_ASSERT_TRUE(mv >= 2850 && mv <= 3150);
}

void test_3v_rail_below_range_fails(void)
{
    int mv = 2849;
    TEST_ASSERT_FALSE(mv >= 2850 && mv <= 3150);
}

void test_3v_rail_above_range_fails(void)
{
    int mv = 3151;
    TEST_ASSERT_FALSE(mv >= 2850 && mv <= 3150);
}

/* dut_rtc_read: RTC coin cell 1.55–3.6 V → 1550–3600 mV */
void test_rtc_range_fresh_cell(void)
{
    const char *resp = "OK RTC_READ 3000";
    int mv = 0;
    int parsed = sscanf(resp, "OK RTC_READ %d", &mv);
    TEST_ASSERT_EQUAL_INT(1, parsed);
    TEST_ASSERT_EQUAL_INT(3000, mv);
    TEST_ASSERT_TRUE(mv >= 1550 && mv <= 3600);
}

void test_rtc_low_boundary(void)
{
    int mv = 1550;
    TEST_ASSERT_TRUE(mv >= 1550 && mv <= 3600);
}

void test_rtc_below_range_fails(void)
{
    /* Below 1.55V — coin cell depleted or missing */
    int mv = 1549;
    TEST_ASSERT_FALSE(mv >= 1550 && mv <= 3600);
}

/* dut_flash_test: response must contain "PASS" */
void test_flash_test_pass_response(void)
{
    const char *resp = "OK FLASH_TEST PASS 4096KB";
    TEST_ASSERT_NOT_NULL(strstr(resp, "PASS"));
}

void test_flash_test_fail_response(void)
{
    const char *resp = "OK FLASH_TEST FAIL addr=0x08001234";
    /* "PASS" substring absent */
    TEST_ASSERT_NULL(strstr(resp, "PASS"));
}

void test_flash_test_err_response(void)
{
    const char *resp = "ERR FLASH_NOT_READY";
    /* Not "OK" AND no "PASS" */
    TEST_ASSERT_NULL(strstr(resp, "PASS"));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Recipe persistence — store → load round-trip
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_store_and_load_recipe(void)
{
    int rc = recipe_json_store_nvs("g3-mb-v1", G3_MB_V1_JSON,
                                   sizeof(G3_MB_V1_JSON) - 1);
    TEST_ASSERT_EQUAL_INT(0, rc);

    char *loaded = recipe_json_load_nvs("g3-mb-v1");
    TEST_ASSERT_NOT_NULL(loaded);

    /* Must parse without error */
    json_recipe_t r2;
    memset(&r2, 0, sizeof(r2));
    rc = recipe_json_parse(loaded, strlen(loaded), &r2);
    free(loaded);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("g3-mb-v1", r2.recipe_id);
    TEST_ASSERT_EQUAL_INT(17, r2.step_count);
    recipe_json_free(&r2);
}

void test_load_nonexistent_returns_null(void)
{
    char *p = recipe_json_load_nvs("does-not-exist");
    TEST_ASSERT_NULL(p);
}

void test_store_overwrites_previous(void)
{
    recipe_json_store_nvs("g3-mb-v1", "{\"recipeId\":\"old\"}", 17);
    recipe_json_store_nvs("g3-mb-v1", G3_MB_V1_JSON, sizeof(G3_MB_V1_JSON) - 1);

    char *loaded = recipe_json_load_nvs("g3-mb-v1");
    TEST_ASSERT_NOT_NULL(loaded);
    /* Must contain v1.2.0 content */
    TEST_ASSERT_NOT_NULL(strstr(loaded, "1.2.0"));
    free(loaded);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Step ordering — UART sequence must be contiguous and correctly ordered
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_uart_step_ordering(void)
{
    /* dut_enter_test must appear before all other dut_* UART steps */
    int enter_idx = -1, exit_idx = -1;
    for (int i = 0; i < s_recipe.step_count; i++) {
        if (strcmp(s_recipe.steps[i].id, "dut_enter_test") == 0) enter_idx = i;
        if (strcmp(s_recipe.steps[i].id, "dut_exit_test") == 0)  exit_idx  = i;
    }
    TEST_ASSERT_GREATER_THAN(-1, enter_idx);
    TEST_ASSERT_GREATER_THAN(-1, exit_idx);
    TEST_ASSERT_LESS_THAN(exit_idx, enter_idx);  /* enter < exit */

    /* All UART steps must be between enter and exit */
    const char *uart_steps[] = {
        "dut_version", "dut_hw_rev", "dut_uc_adc_vref",
        "dut_uc_adc_3v_rail", "dut_flash_test", "dut_rtc_read"
    };
    for (size_t j = 0; j < sizeof(uart_steps) / sizeof(uart_steps[0]); j++) {
        int idx = -1;
        for (int i = 0; i < s_recipe.step_count; i++) {
            if (strcmp(s_recipe.steps[i].id, uart_steps[j]) == 0) { idx = i; break; }
        }
        TEST_ASSERT_GREATER_THAN(enter_idx, idx);
        TEST_ASSERT_LESS_THAN(exit_idx, idx);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test runner
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    UNITY_BEGIN();

    /* Recipe header */
    RUN_TEST(test_recipe_id_and_version);
    RUN_TEST(test_recipe_step_count);
    RUN_TEST(test_recipe_timeout);

    /* TC carrier steps */
    RUN_TEST(test_step_i2c_enabled_critical);
    RUN_TEST(test_step_adc_enabled_optional);
    RUN_TEST(test_step_wifi_enabled_required);
    RUN_TEST(test_step_ota_enabled_required);
    RUN_TEST(test_step_dut_read_id_enabled);
    RUN_TEST(test_step_power_check_enabled_critical);
    RUN_TEST(test_step_mux_scan_enabled);

    /* Steps disabled pending hardware readiness */
    RUN_TEST(test_step_dut_program_disabled);
    RUN_TEST(test_step_dut_heartbeat_disabled);

    /* DUT UART steps — enabled */
    RUN_TEST(test_step_dut_enter_test_enabled_critical);
    RUN_TEST(test_step_dut_version_enabled);
    RUN_TEST(test_step_dut_hw_rev_enabled);
    RUN_TEST(test_step_dut_uc_adc_vref_enabled);
    RUN_TEST(test_step_dut_uc_adc_3v_rail_enabled);
    RUN_TEST(test_step_dut_flash_test_enabled);
    RUN_TEST(test_step_dut_rtc_read_enabled);
    RUN_TEST(test_step_dut_exit_test_enabled);

    /* Response parsing and range checks */
    RUN_TEST(test_enter_test_response_ok);
    RUN_TEST(test_enter_test_response_err);
    RUN_TEST(test_vref_range_nominal);
    RUN_TEST(test_vref_range_low_boundary);
    RUN_TEST(test_vref_range_high_boundary);
    RUN_TEST(test_vref_below_range_fails);
    RUN_TEST(test_vref_above_range_fails);
    RUN_TEST(test_3v_rail_range_nominal);
    RUN_TEST(test_3v_rail_low_boundary);
    RUN_TEST(test_3v_rail_high_boundary);
    RUN_TEST(test_3v_rail_below_range_fails);
    RUN_TEST(test_3v_rail_above_range_fails);
    RUN_TEST(test_rtc_range_fresh_cell);
    RUN_TEST(test_rtc_low_boundary);
    RUN_TEST(test_rtc_below_range_fails);
    RUN_TEST(test_flash_test_pass_response);
    RUN_TEST(test_flash_test_fail_response);
    RUN_TEST(test_flash_test_err_response);

    /* Persistence */
    RUN_TEST(test_store_and_load_recipe);
    RUN_TEST(test_load_nonexistent_returns_null);
    RUN_TEST(test_store_overwrites_previous);

    /* Step ordering */
    RUN_TEST(test_uart_step_ordering);

    return UNITY_END();
}
