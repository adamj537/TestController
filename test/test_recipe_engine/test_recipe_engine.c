/**
 * @file test_recipe_engine.c
 * @brief Regression tests for recipe_engine step pass/fail evaluation.
 *
 * Bug: recipe_engine evaluated step_ok from selftest_get_total()/selftest_get_pass()
 * (updated only by report()), while all recipe primitives call selftest_check_record()
 * (updates s_ncheck only).  Every primitive step saw step_checks==0 and defaulted to
 * PASS regardless of actual check results.
 *
 * Fix: engine now reads ncheck delta + checks[].pass to evaluate step_ok, falling
 * back to total/pass counters only when no check records were registered (legacy path).
 *
 * Tests verify:
 *   1. Primitive calling selftest_check_record(false) → step FAIL
 *   2. Primitive calling selftest_check_record(true)  → step PASS
 *   3. Primitive with mixed checks (any false)        → step FAIL
 *   4. Primitive registering no checks                → step PASS (no-op default)
 *   5. OPTIONAL step fail does not fail the recipe
 *   6. Multi-step recipe records each step independently
 */

#ifndef NATIVE_BUILD
#define NATIVE_BUILD
#endif

/* ── strlcpy (not in glibc) ──────────────────────────────────────────────── */
#include <string.h>
#include <stddef.h>
static size_t _strlcpy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size > 0) {
        size_t n = (len < size - 1) ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}
#define strlcpy _strlcpy

/* ── tc_mqtt.h is shadowed by local stub (defines tc_mqtt_check_t) ────────── */
/* Implementations of the declared functions come after. */
#include "tc_mqtt.h"

static bool s_last_step_passed  = false;
static int  s_step_result_count = 0;

void tc_mqtt_publish_step_result(int step_index, const char *step_id,
                                  const char *step_status, bool passed,
                                  uint32_t duration_ms,
                                  const step_meas_t *meas)
{
    (void)step_index; (void)step_id; (void)step_status; (void)duration_ms; (void)meas;
    s_last_step_passed = passed;
    s_step_result_count++;
}

void tc_mqtt_publish_test_progress(const char *event_type, bool in_progress,
                                    int step_index, int step_total,
                                    const char *step_name, const char *step_status)
{
    (void)event_type; (void)in_progress; (void)step_index;
    (void)step_total; (void)step_name;   (void)step_status;
}

/* ── Minimal selftest check buffer (mirrors cmd_selftest.c internals) ─────── */

#define MAX_CHECKS 128
static tc_mqtt_check_t s_checks[MAX_CHECKS];
static int             s_ncheck;
static int             s_pass;
static int             s_total;

void selftest_reset_checks(void)
{
    s_ncheck = 0;
    s_pass   = 0;
    s_total  = 0;
}

void selftest_check_record(const char *id, bool pass)
{
    if (s_ncheck >= MAX_CHECKS) return;
    s_checks[s_ncheck].id        = id;
    s_checks[s_ncheck].pass      = pass;
    s_checks[s_ncheck].has_value = false;
    s_checks[s_ncheck].value     = 0;
    s_ncheck++;
}

void selftest_check_record_mv(const char *id, bool pass, int mv)
{
    if (s_ncheck >= MAX_CHECKS) return;
    s_checks[s_ncheck].id        = id;
    s_checks[s_ncheck].pass      = pass;
    s_checks[s_ncheck].has_value = true;
    s_checks[s_ncheck].value     = mv;
    s_ncheck++;
}

int                     selftest_get_ncheck(void) { return s_ncheck;  }
int                     selftest_get_pass(void)   { return s_pass;    }
int                     selftest_get_total(void)  { return s_total;   }
const tc_mqtt_check_t  *selftest_get_checks(void) { return s_checks;  }

/* ── Primitive dispatch ───────────────────────────────────────────────────── */
#include "../../src/recipe_primitives.h"

static bool s_prim_check_enabled = false;
static bool s_prim_check_value   = true;
static bool s_prim_mixed         = false;

static void test_primitive(const struct cJSON *params)
{
    (void)params;
    if (!s_prim_check_enabled) return;
    if (s_prim_mixed) {
        selftest_check_record("mixed_a", true);
        selftest_check_record("mixed_b", false);
    } else {
        selftest_check_record("single", s_prim_check_value);
    }
}

const primitive_entry_t *recipe_primitives_table(int *count_out)
{
    if (count_out) *count_out = 0;
    return NULL;
}

primitive_fn_t recipe_primitives_lookup(const char *id)
{
    if (strcmp(id, "test_prim") == 0) return test_primitive;
    return NULL;
}

/* ── Other stubs required by recipe_engine.c ─────────────────────────────── */
void g3_critical_abort(void) {}

/* meas_log stub — recipe_engine.c only calls meas_log_reset(); no submodule path needed. */
void meas_log_reset(void) {}

/* ── Unit under test ─────────────────────────────────────────────────────── */
#include "../../src/recipe_engine.h"
#include "../../src/recipe_engine.c"

/* ── Unity ────────────────────────────────────────────────────────────────── */
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static json_recipe_t make_single_step(const char *on_error, step_criticality_t crit)
{
    json_recipe_t r;
    memset(&r, 0, sizeof(r));
    strlcpy(r.recipe_id,      "test",    sizeof(r.recipe_id));
    strlcpy(r.recipe_version, "0.0.1",  sizeof(r.recipe_version));
    r.timeout_ms = 10000;
    r.step_count = 1;
    r._root      = NULL;

    json_recipe_step_t *s = &r.steps[0];
    strlcpy(s->id,        "step1",     sizeof(s->id));
    strlcpy(s->primitive, "test_prim", sizeof(s->primitive));
    strlcpy(s->label,     "Test Step", sizeof(s->label));
    strlcpy(s->on_error,  on_error,    sizeof(s->on_error));
    s->criticality = crit;
    s->enabled     = true;
    s->params      = NULL;
    return r;
}

static void reset_state(void)
{
    selftest_reset_checks();
    s_last_step_passed   = false;
    s_step_result_count  = 0;
    s_prim_check_enabled = false;
    s_prim_check_value   = true;
    s_prim_mixed         = false;
}

/* ── Tests ───────────────────────────────────────────────────────────────── */

void setUp(void)    { reset_state(); }
void tearDown(void) {}

/* 1. selftest_check_record(false) → step FAIL */
void test_check_record_false_reports_fail(void)
{
    s_prim_check_enabled = true;
    s_prim_check_value   = false;

    json_recipe_t r = make_single_step("skip", STEP_CRITICALITY_REQUIRED);
    recipe_run_result_t result;
    recipe_engine_run(&r, &result);

    TEST_ASSERT_EQUAL(1, s_step_result_count);
    TEST_ASSERT_FALSE(s_last_step_passed);
    TEST_ASSERT_FALSE(result.step_results[0].passed);
    TEST_ASSERT_EQUAL(RECIPE_RESULT_FAIL, result.outcome);
    TEST_ASSERT_EQUAL(0, result.pass_count);
}

/* 2. selftest_check_record(true) → step PASS */
void test_check_record_true_reports_pass(void)
{
    s_prim_check_enabled = true;
    s_prim_check_value   = true;

    json_recipe_t r = make_single_step("skip", STEP_CRITICALITY_REQUIRED);
    recipe_run_result_t result;
    recipe_engine_run(&r, &result);

    TEST_ASSERT_EQUAL(1, s_step_result_count);
    TEST_ASSERT_TRUE(s_last_step_passed);
    TEST_ASSERT_TRUE(result.step_results[0].passed);
    TEST_ASSERT_EQUAL(RECIPE_RESULT_PASS, result.outcome);
    TEST_ASSERT_EQUAL(1, result.pass_count);
}

/* 3. No checks registered → step PASS (default) */
void test_no_checks_defaults_to_pass(void)
{
    s_prim_check_enabled = false;

    json_recipe_t r = make_single_step("skip", STEP_CRITICALITY_REQUIRED);
    recipe_run_result_t result;
    recipe_engine_run(&r, &result);

    TEST_ASSERT_TRUE(s_last_step_passed);
    TEST_ASSERT_EQUAL(RECIPE_RESULT_PASS, result.outcome);
}

/* 4. Mixed checks (pass + fail) → step FAIL */
void test_mixed_checks_reports_fail(void)
{
    s_prim_check_enabled = true;
    s_prim_mixed         = true;

    json_recipe_t r = make_single_step("skip", STEP_CRITICALITY_REQUIRED);
    recipe_run_result_t result;
    recipe_engine_run(&r, &result);

    TEST_ASSERT_FALSE(s_last_step_passed);
    TEST_ASSERT_FALSE(result.step_results[0].passed);
    TEST_ASSERT_EQUAL(RECIPE_RESULT_FAIL, result.outcome);
}

/* 5. OPTIONAL fail does not fail the recipe */
void test_optional_fail_does_not_fail_recipe(void)
{
    s_prim_check_enabled = true;
    s_prim_check_value   = false;

    json_recipe_t r = make_single_step("skip", STEP_CRITICALITY_OPTIONAL);
    recipe_run_result_t result;
    recipe_engine_run(&r, &result);

    TEST_ASSERT_FALSE(s_last_step_passed);
    TEST_ASSERT_EQUAL(RECIPE_RESULT_PASS, result.outcome);
}

/* 6. Multi-step: each step's result recorded independently */
void test_multi_step_results_are_independent(void)
{
    json_recipe_t r;
    memset(&r, 0, sizeof(r));
    strlcpy(r.recipe_id, "multi", sizeof(r.recipe_id));
    r.timeout_ms = 10000;
    r.step_count = 2;

    /* Step 0: OPTIONAL, will fail */
    strlcpy(r.steps[0].id,        "s0_fail",   sizeof(r.steps[0].id));
    strlcpy(r.steps[0].primitive, "test_prim", sizeof(r.steps[0].primitive));
    strlcpy(r.steps[0].on_error,  "skip",      sizeof(r.steps[0].on_error));
    r.steps[0].criticality = STEP_CRITICALITY_OPTIONAL;
    r.steps[0].enabled     = true;

    /* Step 1: REQUIRED, will also fail (same primitive global) */
    strlcpy(r.steps[1].id,        "s1_fail",   sizeof(r.steps[1].id));
    strlcpy(r.steps[1].primitive, "test_prim", sizeof(r.steps[1].primitive));
    strlcpy(r.steps[1].on_error,  "skip",      sizeof(r.steps[1].on_error));
    r.steps[1].criticality = STEP_CRITICALITY_REQUIRED;
    r.steps[1].enabled     = true;

    s_prim_check_enabled = true;
    s_prim_check_value   = false;

    recipe_run_result_t result;
    recipe_engine_run(&r, &result);

    TEST_ASSERT_EQUAL(2, s_step_result_count);
    TEST_ASSERT_EQUAL(2, result.total_count);
    TEST_ASSERT_FALSE(result.step_results[0].passed);
    TEST_ASSERT_FALSE(result.step_results[1].passed);
    TEST_ASSERT_EQUAL(0, result.pass_count);
    TEST_ASSERT_EQUAL(RECIPE_RESULT_FAIL, result.outcome);
}

/* 7. requires_pass: step skipped when a prior REQUIRED step failed */
void test_requires_pass_skips_on_prior_required_fail(void)
{
    json_recipe_t r;
    memset(&r, 0, sizeof(r));
    strlcpy(r.recipe_id, "rp_skip", sizeof(r.recipe_id));
    r.timeout_ms = 10000;
    r.step_count = 2;

    /* Step 0: REQUIRED, will fail */
    strlcpy(r.steps[0].id,        "s0_req",    sizeof(r.steps[0].id));
    strlcpy(r.steps[0].primitive, "test_prim", sizeof(r.steps[0].primitive));
    strlcpy(r.steps[0].on_error,  "skip",      sizeof(r.steps[0].on_error));
    r.steps[0].criticality  = STEP_CRITICALITY_REQUIRED;
    r.steps[0].enabled      = true;

    /* Step 1: CRITICAL + requires_pass — must be skipped */
    strlcpy(r.steps[1].id,        "s1_gated",  sizeof(r.steps[1].id));
    strlcpy(r.steps[1].primitive, "test_prim", sizeof(r.steps[1].primitive));
    strlcpy(r.steps[1].on_error,  "abort",     sizeof(r.steps[1].on_error));
    r.steps[1].criticality  = STEP_CRITICALITY_CRITICAL;
    r.steps[1].enabled      = true;
    r.steps[1].requires_pass = true;

    s_prim_check_enabled = true;
    s_prim_check_value   = false;  /* step 0 fails */

    recipe_run_result_t result;
    recipe_engine_run(&r, &result);

    /* Step 0 ran (and failed); step 1 was gated out — only 1 MQTT result publish */
    TEST_ASSERT_EQUAL(1, s_step_result_count);
    TEST_ASSERT_FALSE(result.step_results[0].passed);
    /* Outcome is FAIL (required failure), not ABORT (gated step never ran) */
    TEST_ASSERT_EQUAL(RECIPE_RESULT_FAIL, result.outcome);
}

/* 8. requires_pass: step runs when no prior required failures */
void test_requires_pass_runs_on_clean_slate(void)
{
    json_recipe_t r;
    memset(&r, 0, sizeof(r));
    strlcpy(r.recipe_id, "rp_run", sizeof(r.recipe_id));
    r.timeout_ms = 10000;
    r.step_count = 2;

    /* Step 0: REQUIRED, will pass */
    strlcpy(r.steps[0].id,        "s0_pass",   sizeof(r.steps[0].id));
    strlcpy(r.steps[0].primitive, "test_prim", sizeof(r.steps[0].primitive));
    strlcpy(r.steps[0].on_error,  "skip",      sizeof(r.steps[0].on_error));
    r.steps[0].criticality  = STEP_CRITICALITY_REQUIRED;
    r.steps[0].enabled      = true;

    /* Step 1: requires_pass=true — runs because step 0 passed */
    strlcpy(r.steps[1].id,        "s1_gated",  sizeof(r.steps[1].id));
    strlcpy(r.steps[1].primitive, "test_prim", sizeof(r.steps[1].primitive));
    strlcpy(r.steps[1].on_error,  "abort",     sizeof(r.steps[1].on_error));
    r.steps[1].criticality  = STEP_CRITICALITY_CRITICAL;
    r.steps[1].enabled      = true;
    r.steps[1].requires_pass = true;

    s_prim_check_enabled = true;
    s_prim_check_value   = true;  /* both pass */

    recipe_run_result_t result;
    recipe_engine_run(&r, &result);

    TEST_ASSERT_EQUAL(2, s_step_result_count);
    TEST_ASSERT_TRUE(result.step_results[0].passed);
    TEST_ASSERT_TRUE(result.step_results[1].passed);
    TEST_ASSERT_EQUAL(RECIPE_RESULT_PASS, result.outcome);
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_check_record_false_reports_fail);
    RUN_TEST(test_check_record_true_reports_pass);
    RUN_TEST(test_no_checks_defaults_to_pass);
    RUN_TEST(test_mixed_checks_reports_fail);
    RUN_TEST(test_optional_fail_does_not_fail_recipe);
    RUN_TEST(test_multi_step_results_are_independent);
    RUN_TEST(test_requires_pass_skips_on_prior_required_fail);
    RUN_TEST(test_requires_pass_runs_on_clean_slate);
    return UNITY_END();
}
