/**
 * @file test_recipe_v2.c
 * @brief Native tests for g3-mb-v2 recipe — step contracts, branch structure,
 *        primitive coverage, and param validation.
 *
 * Reads the actual recipes/g3-mb-v2.json from disk so any recipe JSON change
 * is automatically covered.  Complements test_recipe_steps.c (which tests
 * g3-mb-v1 and generic parser logic).
 *
 * Build target: native (env:native).
 */

#ifndef NATIVE_BUILD
#define NATIVE_BUILD
#endif

/* ── cJSON ─────────────────────────────────────────────────────────────────── */
#include "cJSON.h"
#include "cJSON.c"

/* ── ESP-IDF stubs ────────────────────────────────────────────────────────── */
/* Stub headers live in this test directory, shadowing real ESP-IDF headers. */

/* ── Storage mock ──────────────────────────────────────────────────────────── */
#include "../../storage/storage.h"
#include "../../storage/storage_mock.c"

/* ── Unit under test ───────────────────────────────────────────────────────── */
#include "../../src/recipe_primitives.h"
#include "../../src/recipe_engine.h"
#include "../../src/recipe_json.h"

/* Stubs — same as test_recipe_steps */
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

int recipe_engine_run(const json_recipe_t *recipe, recipe_run_result_t *result)
{
    (void)recipe;
    if (result) {
        result->outcome     = RECIPE_RESULT_PASS;
        result->pass_count  = 0;
        result->total_count = 0;
        result->duration_ms = 0;
        result->failed_step[0] = '\0';
    }
    return 0;
}

#include "../../src/recipe_json.c"

/* ── Unity ─────────────────────────────────────────────────────────────────── */
#include "unity.h"

#include <stdio.h>
#include <stdlib.h>

/* ── Load recipe from file ─────────────────────────────────────────────────── */

static char *s_json_buf;
static json_recipe_t s_recipe;

/* Try multiple paths — native tests may run from different CWDs */
static const char *RECIPE_PATHS[] = {
    "recipes/g3-mb-v2.json",
    "../recipes/g3-mb-v2.json",
    "../../recipes/g3-mb-v2.json",
    "embedded/tester-client/recipes/g3-mb-v2.json",
    NULL
};

static char *load_recipe_file(void)
{
    FILE *f = NULL;
    for (int i = 0; RECIPE_PATHS[i]; i++) {
        f = fopen(RECIPE_PATHS[i], "r");
        if (f) break;
    }
    if (!f) {
        /* Last resort: absolute path */
        f = fopen("/home/cbasta/G3-MB-Tester/embedded/tester-client/recipes/g3-mb-v2.json", "r");
    }
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, sz, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

/* ── Helpers ───────────────────────────────────────────────────────────────── */

static const json_recipe_step_t *find_step(const char *id)
{
    for (int i = 0; i < s_recipe.step_count; i++) {
        if (strcmp(s_recipe.steps[i].id, id) == 0)
            return &s_recipe.steps[i];
    }
    return NULL;
}

static int find_step_index(const char *id)
{
    for (int i = 0; i < s_recipe.step_count; i++) {
        if (strcmp(s_recipe.steps[i].id, id) == 0)
            return i;
    }
    return -1;
}

/* Count steps using a given primitive */
static int count_primitive(const char *primitive)
{
    int n = 0;
    for (int i = 0; i < s_recipe.step_count; i++) {
        if (strcmp(s_recipe.steps[i].primitive, primitive) == 0)
            n++;
    }
    return n;
}

/* ── Setup / Teardown ──────────────────────────────────────────────────────── */

void setUp(void)
{
    Storage_Mock_Reset();
    Storage_Init();
    memset(&s_recipe, 0, sizeof(s_recipe));

    if (!s_json_buf) {
        s_json_buf = load_recipe_file();
        TEST_ASSERT_NOT_NULL_MESSAGE(s_json_buf,
            "Could not load recipes/g3-mb-v2.json — check CWD");
    }
    int rc = recipe_json_parse(s_json_buf, strlen(s_json_buf), &s_recipe);
    TEST_ASSERT_EQUAL_MESSAGE(0, rc, "g3-mb-v2 JSON must parse without error");
}

void tearDown(void)
{
    recipe_json_free(&s_recipe);
    Storage_Mock_Reset();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Recipe header
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_v2_recipe_id(void)
{
    TEST_ASSERT_EQUAL_STRING("g3-mb-v2", s_recipe.recipe_id);
}

void test_v2_recipe_version(void)
{
    TEST_ASSERT_EQUAL_STRING("2.1.0", s_recipe.recipe_version);
}

void test_v2_timeout_300s(void)
{
    TEST_ASSERT_EQUAL_UINT32(300000, s_recipe.timeout_ms);
}

void test_v2_step_count(void)
{
    /* 73 steps as of v2.0.0 — must fit in JSON_RECIPE_STEPS_MAX (96) */
    TEST_ASSERT_GREATER_OR_EQUAL(60, s_recipe.step_count);
    TEST_ASSERT_LESS_OR_EQUAL(JSON_RECIPE_STEPS_MAX, s_recipe.step_count);
}

void test_v2_no_recovery_branches(void)
{
    TEST_ASSERT_EQUAL_INT(0, s_recipe.recovery_branch_count);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Steps that require unconfirmed pin assignments are disabled until
 * schematic verification is complete.
 * ═══════════════════════════════════════════════════════════════════════════ */

static const char *s_deferred_steps[] = {
    "dut_uc_adc_vref",      /* ADC pin assignment unconfirmed */
    "dut_uc_adc_3v_rail",   /* ADC pin assignment unconfirmed */
    "dut_flash_test",       /* flash uses QUADSPI, not SPI1 */
};
#define NUM_DEFERRED (sizeof(s_deferred_steps) / sizeof(s_deferred_steps[0]))

static bool is_deferred(const char *id)
{
    for (size_t i = 0; i < NUM_DEFERRED; i++) {
        if (strcmp(id, s_deferred_steps[i]) == 0) return true;
    }
    return false;
}

void test_v2_all_steps_enabled(void)
{
    for (int i = 0; i < s_recipe.step_count; i++) {
        if (is_deferred(s_recipe.steps[i].id)) {
            TEST_ASSERT_FALSE_MESSAGE(s_recipe.steps[i].enabled,
                "Deferred step should be disabled");
            continue;
        }
        char msg[128];
        snprintf(msg, sizeof(msg), "Step '%s' must be enabled in v2",
                 s_recipe.steps[i].id);
        TEST_ASSERT_TRUE_MESSAGE(s_recipe.steps[i].enabled, msg);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CRITICAL steps — these abort on failure
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_v2_critical_i2c(void)
{
    const json_recipe_step_t *s = find_step("i2c");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_CRITICAL, s->criticality);
    TEST_ASSERT_EQUAL_STRING("abort", s->on_error);
}

void test_v2_critical_dut_program(void)
{
    const json_recipe_step_t *s = find_step("dut_program");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_CRITICAL, s->criticality);
    TEST_ASSERT_EQUAL_STRING("abort", s->on_error);
    TEST_ASSERT_NOT_NULL(s->params);
}

void test_v2_critical_power_check(void)
{
    const json_recipe_step_t *s = find_step("power_check");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_CRITICAL, s->criticality);
    TEST_ASSERT_EQUAL_STRING("abort", s->on_error);
}

void test_v2_critical_heartbeat(void)
{
    const json_recipe_step_t *s = find_step("dut_heartbeat");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_CRITICAL, s->criticality);
    TEST_ASSERT_EQUAL_STRING("abort", s->on_error);
}

void test_v2_critical_enter_test(void)
{
    const json_recipe_step_t *s = find_step("dut_enter_test");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_CRITICAL, s->criticality);
    TEST_ASSERT_EQUAL_STRING("abort", s->on_error);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * OPTIONAL steps — advisory only
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_v2_optional_adc(void)
{
    const json_recipe_step_t *s = find_step("adc");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_OPTIONAL, s->criticality);
}

void test_v2_optional_mux_scan(void)
{
    const json_recipe_step_t *s = find_step("mux_scan");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(STEP_CRITICALITY_OPTIONAL, s->criticality);
}

void test_v2_gpio_clear_steps_optional(void)
{
    /* All gpio_clear steps should be OPTIONAL — cleanup only */
    for (int i = 0; i < s_recipe.step_count; i++) {
        if (strcmp(s_recipe.steps[i].primitive, "dut_gpio_clear") == 0) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "Step '%s' (dut_gpio_clear) should be OPTIONAL",
                     s_recipe.steps[i].id);
            TEST_ASSERT_EQUAL_INT_MESSAGE(
                STEP_CRITICALITY_OPTIONAL, s_recipe.steps[i].criticality, msg);
        }
    }
}

void test_v2_branch_disable_steps_optional(void)
{
    /* All dut_pwr_disable steps should be OPTIONAL — cleanup */
    for (int i = 0; i < s_recipe.step_count; i++) {
        if (strcmp(s_recipe.steps[i].primitive, "dut_pwr_disable") == 0) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "Step '%s' (dut_pwr_disable) should be OPTIONAL",
                     s_recipe.steps[i].id);
            TEST_ASSERT_EQUAL_INT_MESSAGE(
                STEP_CRITICALITY_OPTIONAL, s_recipe.steps[i].criticality, msg);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Primitive coverage — all v2 primitive types present
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_v2_has_primitive_i2c(void)
{
    TEST_ASSERT_GREATER_OR_EQUAL(1, count_primitive("i2c"));
}

void test_v2_has_primitive_branch_test(void)
{
    /* 4 branches with V/I monitoring */
    TEST_ASSERT_GREATER_OR_EQUAL(4, count_primitive("branch_test"));
}

void test_v2_has_primitive_dut_gpio_set(void)
{
    TEST_ASSERT_GREATER_OR_EQUAL(10, count_primitive("dut_gpio_set"));
}

void test_v2_has_primitive_dut_gpio_clear(void)
{
    TEST_ASSERT_GREATER_OR_EQUAL(10, count_primitive("dut_gpio_clear"));
}

void test_v2_has_primitive_mux_read(void)
{
    TEST_ASSERT_GREATER_OR_EQUAL(10, count_primitive("mux_read"));
}

void test_v2_has_primitive_dut_pwr_enable(void)
{
    /* Branches 1-4, 6 = at least 5 enables */
    TEST_ASSERT_GREATER_OR_EQUAL(5, count_primitive("dut_pwr_enable"));
}

void test_v2_has_primitive_dut_pwr_disable(void)
{
    TEST_ASSERT_GREATER_OR_EQUAL(5, count_primitive("dut_pwr_disable"));
}

void test_v2_has_primitive_dut_peripheral_adc_read(void)
{
    TEST_ASSERT_GREATER_OR_EQUAL(4, count_primitive("dut_peripheral_adc_read"));
}

void test_v2_has_primitive_dut_i2c_scan(void)
{
    TEST_ASSERT_GREATER_OR_EQUAL(2, count_primitive("dut_i2c_scan"));
}

void test_v2_has_primitive_button_test(void)
{
    TEST_ASSERT_GREATER_OR_EQUAL(2, count_primitive("button_test"));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Branch structure — enable → branch_test → steps → disable pattern
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Helper: verify a branch has enable before disable, and branch_test between them */
static void assert_branch_structure(int branch_num)
{
    char enable_id[32], disable_id[32], vi_id[32];
    snprintf(enable_id,  sizeof(enable_id),  "br%d_enable",  branch_num);
    snprintf(disable_id, sizeof(disable_id), "br%d_disable", branch_num);
    snprintf(vi_id,      sizeof(vi_id),      "br%d_vi",      branch_num);

    int ena_idx = find_step_index(enable_id);
    int dis_idx = find_step_index(disable_id);
    int vi_idx  = find_step_index(vi_id);

    char msg[128];
    snprintf(msg, sizeof(msg), "Branch %d must have enable step", branch_num);
    TEST_ASSERT_GREATER_THAN_MESSAGE(-1, ena_idx, msg);

    snprintf(msg, sizeof(msg), "Branch %d must have disable step", branch_num);
    TEST_ASSERT_GREATER_THAN_MESSAGE(-1, dis_idx, msg);

    snprintf(msg, sizeof(msg), "Branch %d enable must precede disable", branch_num);
    TEST_ASSERT_LESS_THAN_MESSAGE(dis_idx, ena_idx, msg);

    /* Branch test is between enable and disable (if present) */
    if (vi_idx >= 0) {
        snprintf(msg, sizeof(msg), "Branch %d V/I must follow enable", branch_num);
        TEST_ASSERT_GREATER_THAN_MESSAGE(ena_idx, vi_idx, msg);
        snprintf(msg, sizeof(msg), "Branch %d V/I must precede disable", branch_num);
        TEST_ASSERT_LESS_THAN_MESSAGE(dis_idx, vi_idx, msg);
    }
}

void test_v2_branch1_structure(void) { assert_branch_structure(1); }
void test_v2_branch2_structure(void) { assert_branch_structure(2); }
void test_v2_branch3_structure(void) { assert_branch_structure(3); }
void test_v2_branch4_structure(void) { assert_branch_structure(4); }
void test_v2_branch6_structure(void) { assert_branch_structure(6); }

void test_v2_branch_ordering(void)
{
    /* Branches must execute in order: 1, 2, 3, 4, then 6 */
    int b1 = find_step_index("br1_enable");
    int b2 = find_step_index("br2_enable");
    int b3 = find_step_index("br3_enable");
    int b4 = find_step_index("br4_enable");
    int b6 = find_step_index("br6_enable");

    TEST_ASSERT_LESS_THAN(b2, b1);
    TEST_ASSERT_LESS_THAN(b3, b2);
    TEST_ASSERT_LESS_THAN(b4, b3);
    TEST_ASSERT_LESS_THAN(b6, b4);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * GPIO set → mux_read → gpio_clear triplet pattern
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Verify a set/read/clear triplet appears in order */
static void assert_gpio_triplet(const char *set_id, const char *read_id,
                                const char *clear_id)
{
    int set_idx   = find_step_index(set_id);
    int read_idx  = find_step_index(read_id);
    int clear_idx = find_step_index(clear_id);

    char msg[128];
    snprintf(msg, sizeof(msg), "GPIO triplet: %s must exist", set_id);
    TEST_ASSERT_GREATER_THAN_MESSAGE(-1, set_idx, msg);
    snprintf(msg, sizeof(msg), "GPIO triplet: %s must exist", read_id);
    TEST_ASSERT_GREATER_THAN_MESSAGE(-1, read_idx, msg);
    snprintf(msg, sizeof(msg), "GPIO triplet: %s must exist", clear_id);
    TEST_ASSERT_GREATER_THAN_MESSAGE(-1, clear_idx, msg);

    snprintf(msg, sizeof(msg), "%s must precede %s", set_id, read_id);
    TEST_ASSERT_LESS_THAN_MESSAGE(read_idx, set_idx, msg);
    snprintf(msg, sizeof(msg), "%s must precede %s", read_id, clear_id);
    TEST_ASSERT_LESS_THAN_MESSAGE(clear_idx, read_idx, msg);
}

void test_v2_br1_flashlight_triplet(void)
{
    assert_gpio_triplet("br1_flashlight_ena", "br1_flashlight_read",
                        "br1_flashlight_clear");
}

void test_v2_br2_led_r_triplet(void)
{
    assert_gpio_triplet("br2_led_r", "br2_led_r_read", "br2_led_r_clear");
}

void test_v2_br5_lcd_sck_triplet(void)
{
    assert_gpio_triplet("br5_lcd_sck", "br5_lcd_sck_read", "br5_lcd_sck_clear");
}

void test_v2_br6_bt_gpio1_triplet(void)
{
    assert_gpio_triplet("br6_bt_gpio1", "br6_bt_gpio1_read",
                        "br6_bt_gpio1_clear");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * UART step ordering — enter_test before all DUT steps, exit_test after
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_v2_uart_ordering(void)
{
    int enter_idx = find_step_index("dut_enter_test");
    int exit_idx  = find_step_index("dut_exit_test");

    TEST_ASSERT_GREATER_THAN(-1, enter_idx);
    TEST_ASSERT_GREATER_THAN(-1, exit_idx);
    TEST_ASSERT_LESS_THAN(exit_idx, enter_idx);

    /* All UART-dependent steps must be between enter and exit */
    const char *uart_steps[] = {
        "dut_version", "dut_hw_rev", "dut_uc_adc_vref", "dut_uc_adc_3v_rail",
        "dut_rtc_read", "dut_flash_test"
    };
    for (size_t j = 0; j < sizeof(uart_steps) / sizeof(uart_steps[0]); j++) {
        int idx = find_step_index(uart_steps[j]);
        if (idx < 0) continue;  /* step may not be present */
        char msg[128];
        snprintf(msg, sizeof(msg), "%s must follow dut_enter_test", uart_steps[j]);
        TEST_ASSERT_GREATER_THAN_MESSAGE(enter_idx, idx, msg);
        snprintf(msg, sizeof(msg), "%s must precede dut_exit_test", uart_steps[j]);
        TEST_ASSERT_LESS_THAN_MESSAGE(exit_idx, idx, msg);
    }
}

void test_v2_pre_test_ordering(void)
{
    /* i2c → dut_program → power_check → dut_heartbeat → dut_enter_test */
    int i2c  = find_step_index("i2c");
    int prog = find_step_index("dut_program");
    int pwr  = find_step_index("power_check");
    int hb   = find_step_index("dut_heartbeat");
    int ent  = find_step_index("dut_enter_test");

    TEST_ASSERT_LESS_THAN(prog, i2c);
    TEST_ASSERT_LESS_THAN(pwr,  prog);
    TEST_ASSERT_LESS_THAN(hb,   pwr);
    TEST_ASSERT_LESS_THAN(ent,  hb);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Params validation — spot-check key step params
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_v2_power_check_params(void)
{
    const json_recipe_step_t *s = find_step("power_check");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_NOT_NULL(s->params);
    TEST_ASSERT_EQUAL_INT(3300, cJSON_GetObjectItem(s->params, "v_nominal_mv")->valueint);
    TEST_ASSERT_EQUAL_INT(5, cJSON_GetObjectItem(s->params, "v_tolerance_pct")->valueint);
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetObjectItem(s->params, "i_min_ma")->valueint);
    TEST_ASSERT_EQUAL_INT(250, cJSON_GetObjectItem(s->params, "i_max_ma")->valueint);
}

void test_v2_dut_program_params(void)
{
    const json_recipe_step_t *s = find_step("dut_program");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_NOT_NULL(s->params);
    TEST_ASSERT_EQUAL_STRING("pfw", cJSON_GetObjectItem(s->params, "target")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(s->params, "verify")));
    TEST_ASSERT_EQUAL_INT(60, cJSON_GetObjectItem(s->params, "timeout_s")->valueint);
}

void test_v2_branch_test_params(void)
{
    /* Spot-check branch 1 */
    const json_recipe_step_t *s = find_step("br1_vi");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_NOT_NULL(s->params);
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetObjectItem(s->params, "branch")->valueint);
    TEST_ASSERT_EQUAL_STRING("VMON1", cJSON_GetObjectItem(s->params, "v_channel")->valuestring);
    TEST_ASSERT_EQUAL_INT(3000, cJSON_GetObjectItem(s->params, "v_min_mv")->valueint);
    TEST_ASSERT_EQUAL_INT(3600, cJSON_GetObjectItem(s->params, "v_max_mv")->valueint);
    TEST_ASSERT_EQUAL_INT(200, cJSON_GetObjectItem(s->params, "settle_ms")->valueint);
}

void test_v2_mux_read_params(void)
{
    /* Spot-check br1_flashlight_read */
    const json_recipe_step_t *s = find_step("br1_flashlight_read");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_NOT_NULL(s->params);
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetObjectItem(s->params, "mux")->valueint);
    TEST_ASSERT_EQUAL_INT(10, cJSON_GetObjectItem(s->params, "ch")->valueint);
    TEST_ASSERT_EQUAL_INT(2400, cJSON_GetObjectItem(s->params, "min_mv")->valueint);
    TEST_ASSERT_EQUAL_INT(3600, cJSON_GetObjectItem(s->params, "max_mv")->valueint);
}

void test_v2_gpio_set_params(void)
{
    const json_recipe_step_t *s = find_step("br1_flashlight_ena");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_NOT_NULL(s->params);
    TEST_ASSERT_EQUAL_STRING("PE5", cJSON_GetObjectItem(s->params, "pin")->valuestring);
    TEST_ASSERT_EQUAL_STRING("HIGH", cJSON_GetObjectItem(s->params, "level")->valuestring);
}

void test_v2_button_test_params(void)
{
    const json_recipe_step_t *s = find_step("br5_button_b");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_NOT_NULL(s->params);
    TEST_ASSERT_EQUAL_STRING("PA11", cJSON_GetObjectItem(s->params, "pin")->valuestring);
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetObjectItem(s->params, "u8_ch")->valueint);
    TEST_ASSERT_EQUAL_INT(50, cJSON_GetObjectItem(s->params, "hold_ms")->valueint);
}

void test_v2_i2c_scan_params(void)
{
    const json_recipe_step_t *s = find_step("br2_i2c_led_driver");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_NOT_NULL(s->params);
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetObjectItem(s->params, "bus")->valueint);
    TEST_ASSERT_EQUAL_STRING("0x40", cJSON_GetObjectItem(s->params, "expected")->valuestring);
}

void test_v2_peripheral_adc_read_params(void)
{
    const json_recipe_step_t *s = find_step("br1_2611_vout");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_NOT_NULL(s->params);
    TEST_ASSERT_EQUAL_STRING("VOUT_2611",
                             cJSON_GetObjectItem(s->params, "channel")->valuestring);
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetObjectItem(s->params, "min_mv")->valueint);
    TEST_ASSERT_EQUAL_INT(500, cJSON_GetObjectItem(s->params, "max_mv")->valueint);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Gap-1/Gap-2 — product FW gate + dut_read_id re-enabled
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_v2_dut_program_product_requires_pass(void)
{
    const json_recipe_step_t *s = find_step("dut_program_product");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE_MESSAGE(s->requires_pass,
        "dut_program_product must have requiresPass:true — never flash failed boards");
}

void test_v2_dut_read_id_enabled(void)
{
    const json_recipe_step_t *s = find_step("dut_read_id");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE_MESSAGE(s->enabled, "dut_read_id must be enabled (Gap-2)");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Persistence round-trip
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_v2_store_and_load(void)
{
    int rc = recipe_json_store_nvs("g3-mb-v2", s_json_buf, strlen(s_json_buf));
    TEST_ASSERT_EQUAL_INT(0, rc);

    char *loaded = recipe_json_load_nvs("g3-mb-v2");
    TEST_ASSERT_NOT_NULL(loaded);

    json_recipe_t r2;
    memset(&r2, 0, sizeof(r2));
    rc = recipe_json_parse(loaded, strlen(loaded), &r2);
    free(loaded);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("g3-mb-v2", r2.recipe_id);
    TEST_ASSERT_EQUAL_INT(s_recipe.step_count, r2.step_count);
    recipe_json_free(&r2);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Unique step IDs — no duplicates
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_v2_unique_step_ids(void)
{
    for (int i = 0; i < s_recipe.step_count; i++) {
        for (int j = i + 1; j < s_recipe.step_count; j++) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Duplicate step ID: '%s' at [%d] and [%d]",
                     s_recipe.steps[i].id, i, j);
            TEST_ASSERT_FALSE_MESSAGE(
                strcmp(s_recipe.steps[i].id, s_recipe.steps[j].id) == 0, msg);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Every step has a valid primitive name (non-empty)
 * ═══════════════════════════════════════════════════════════════════════════ */

void test_v2_all_steps_have_primitive(void)
{
    for (int i = 0; i < s_recipe.step_count; i++) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Step '%s' has empty primitive",
                 s_recipe.steps[i].id);
        TEST_ASSERT_GREATER_THAN_MESSAGE(
            (size_t)0, strlen(s_recipe.steps[i].primitive), msg);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test runner
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    s_json_buf = load_recipe_file();
    if (!s_json_buf) {
        fprintf(stderr, "FATAL: Cannot load g3-mb-v2.json\n");
        return 1;
    }

    UNITY_BEGIN();

    /* Header */
    RUN_TEST(test_v2_recipe_id);
    RUN_TEST(test_v2_recipe_version);
    RUN_TEST(test_v2_timeout_300s);
    RUN_TEST(test_v2_step_count);
    RUN_TEST(test_v2_no_recovery_branches);

    /* All enabled */
    RUN_TEST(test_v2_all_steps_enabled);

    /* Criticality */
    RUN_TEST(test_v2_critical_i2c);
    RUN_TEST(test_v2_critical_dut_program);
    RUN_TEST(test_v2_critical_power_check);
    RUN_TEST(test_v2_critical_heartbeat);
    RUN_TEST(test_v2_critical_enter_test);
    RUN_TEST(test_v2_optional_adc);
    RUN_TEST(test_v2_optional_mux_scan);
    RUN_TEST(test_v2_gpio_clear_steps_optional);
    RUN_TEST(test_v2_branch_disable_steps_optional);

    /* Primitive coverage */
    RUN_TEST(test_v2_has_primitive_i2c);
    RUN_TEST(test_v2_has_primitive_branch_test);
    RUN_TEST(test_v2_has_primitive_dut_gpio_set);
    RUN_TEST(test_v2_has_primitive_dut_gpio_clear);
    RUN_TEST(test_v2_has_primitive_mux_read);
    RUN_TEST(test_v2_has_primitive_dut_pwr_enable);
    RUN_TEST(test_v2_has_primitive_dut_pwr_disable);
    RUN_TEST(test_v2_has_primitive_dut_peripheral_adc_read);
    RUN_TEST(test_v2_has_primitive_dut_i2c_scan);
    RUN_TEST(test_v2_has_primitive_button_test);

    /* Branch structure */
    RUN_TEST(test_v2_branch1_structure);
    RUN_TEST(test_v2_branch2_structure);
    RUN_TEST(test_v2_branch3_structure);
    RUN_TEST(test_v2_branch4_structure);
    RUN_TEST(test_v2_branch6_structure);
    RUN_TEST(test_v2_branch_ordering);

    /* GPIO triplets */
    RUN_TEST(test_v2_br1_flashlight_triplet);
    RUN_TEST(test_v2_br2_led_r_triplet);
    RUN_TEST(test_v2_br5_lcd_sck_triplet);
    RUN_TEST(test_v2_br6_bt_gpio1_triplet);

    /* Step ordering */
    RUN_TEST(test_v2_uart_ordering);
    RUN_TEST(test_v2_pre_test_ordering);

    /* Params validation */
    RUN_TEST(test_v2_power_check_params);
    RUN_TEST(test_v2_dut_program_params);
    RUN_TEST(test_v2_branch_test_params);
    RUN_TEST(test_v2_mux_read_params);
    RUN_TEST(test_v2_gpio_set_params);
    RUN_TEST(test_v2_button_test_params);
    RUN_TEST(test_v2_i2c_scan_params);
    RUN_TEST(test_v2_peripheral_adc_read_params);

    /* Gap-1/Gap-2 */
    RUN_TEST(test_v2_dut_program_product_requires_pass);
    RUN_TEST(test_v2_dut_read_id_enabled);

    /* Persistence */
    RUN_TEST(test_v2_store_and_load);

    /* Integrity */
    RUN_TEST(test_v2_unique_step_ids);
    RUN_TEST(test_v2_all_steps_have_primitive);

    free(s_json_buf);
    return UNITY_END();
}
