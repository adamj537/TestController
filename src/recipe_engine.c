/* recipe_engine.c — Execute a json_recipe_t via the primitive dispatch table.
 *
 * Step criticality:
 *   CRITICAL  — failure aborts immediately; g3_critical_abort() cuts DUT power.
 *   REQUIRED  — failure recorded; remaining steps continue for diagnostic data.
 *   OPTIONAL  — advisory only; never causes recipe to fail.
 *
 * Recovery branches:
 *   on_error = "recovery:<name>" — named branch executed on failure, then step
 *   retried up to max_attempts times before aborting.
 */

#include "recipe_engine.h"
#include "recipe_primitives.h"
#include "cmd_selftest.h"
#include "tc_mqtt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "recipe_eng";

/* Declared in g3_primitives.c — cuts VDUT power immediately on CRITICAL abort. */
extern void g3_critical_abort(void);

/* ── Recovery branch helpers ─────────────────────────────────────────────── */

/* Find a recovery branch by name. Returns NULL if not found. */
static const json_recovery_branch_t *find_branch(const json_recipe_t *recipe,
                                                   const char *name)
{
    for (int i = 0; i < recipe->recovery_branch_count; i++) {
        if (strcmp(recipe->recovery_branches[i].name, name) == 0)
            return &recipe->recovery_branches[i];
    }
    return NULL;
}

/* Execute all steps in a recovery branch.
 * Returns true if every step succeeded; false if any step failed (branch aborts). */
static bool run_recovery_branch(const json_recovery_branch_t *branch)
{
    for (int i = 0; i < branch->step_count; i++) {
        const json_recovery_step_t *rs = &branch->steps[i];

        primitive_fn_t fn = recipe_primitives_lookup(rs->primitive);
        if (!fn) {
            ESP_LOGW(TAG, "Recovery step '%s': primitive '%s' not found — branch aborted",
                     rs->id, rs->primitive);
            return false;
        }

        int checks_before = selftest_get_total();
        int passes_before = selftest_get_pass();

        fn(rs->params);

        int step_checks = selftest_get_total() - checks_before;
        int step_passes = selftest_get_pass()  - passes_before;
        bool step_ok = (step_checks > 0) ? (step_passes == step_checks) : true;

        if (!step_ok) {
            ESP_LOGW(TAG, "Recovery step '%s' failed — branch aborted", rs->id);
            return false;
        }
    }
    return true;
}

/* ── Main execution loop ─────────────────────────────────────────────────── */

int recipe_engine_run(const json_recipe_t *recipe, recipe_run_result_t *result)
{
    if (!recipe || !result) return -1;
    memset(result, 0, sizeof(*result));

    int64_t start_us = esp_timer_get_time();

    selftest_reset_checks();

    int enabled_total = 0;
    for (int i = 0; i < recipe->step_count; i++)
        if (recipe->steps[i].enabled) enabled_total++;

    int  step_num  = 0;
    bool aborted   = false;
    bool any_required_fail = false;  /* tracks REQUIRED failures for final outcome */

    for (int i = 0; i < recipe->step_count; i++) {
        const json_recipe_step_t *step = &recipe->steps[i];

        if (!step->enabled) {
            printf("[SKIP] %s\n", step->id);
            continue;
        }

        primitive_fn_t fn = recipe_primitives_lookup(step->primitive);
        if (!fn) {
            printf("[SKIP] %s (primitive '%s' not found)\n", step->id, step->primitive);
            continue;
        }

        step_num++;
        /* Resolve display status: status_msg override → label → id */
        const char *step_status = step->status_msg[0] ? step->status_msg
                                : (step->label[0]     ? step->label
                                :                       step->id);
        tc_mqtt_publish_test_progress("test_progress", true, step_num, enabled_total,
                                      step->label[0] ? step->label : step->id,
                                      step_status);

        /* ── Execute (with optional retry via recovery branch) ──────────── */

        int attempts    = 0;
        int max_retries = 1;   /* attempt step once by default */
        const json_recovery_branch_t *branch = NULL;
        bool is_recovery = (strncmp(step->on_error, "recovery:", 9) == 0);

        if (is_recovery) {
            branch = find_branch(recipe, step->on_error + 9);
            if (branch) max_retries = 1 + branch->max_attempts; /* 1 initial + N retries */
        }

        bool step_ok = false;
        int  measured_value = 0;
        bool has_value      = false;

        for (attempts = 0; attempts < max_retries; attempts++) {
            int ncheck_before = selftest_get_ncheck();
            int pass_before   = selftest_get_pass();
            int total_before  = selftest_get_total();
            int64_t step_start = esp_timer_get_time();

            fn(step->params);

            int64_t step_end   = esp_timer_get_time();
            int ncheck_after   = selftest_get_ncheck();
            int pass_after     = selftest_get_pass();
            int total_after    = selftest_get_total();

            int step_checks = total_after  - total_before;
            int step_passes = pass_after   - pass_before;
            step_ok = (step_checks > 0) ? (step_passes == step_checks) : true;

            /* Capture last measured value from check buffer */
            if (ncheck_after > ncheck_before) {
                const tc_mqtt_check_t *checks = selftest_get_checks();
                const tc_mqtt_check_t *last   = &checks[ncheck_after - 1];
                has_value      = last->has_value;
                measured_value = last->value;
            }

            /* Record this attempt's duration on first pass-through */
            if (result->step_result_count < JSON_RECIPE_STEPS_MAX) {
                step_result_t *sr = &result->step_results[result->step_result_count];
                strlcpy(sr->step_id, step->id, sizeof(sr->step_id));
                sr->duration_ms    = (uint32_t)((step_end - step_start) / 1000);
                sr->has_value      = has_value;
                sr->measured_value = measured_value;
                /* passed set after retry loop */
            }

            if (step_ok) break;

            /* Step failed — run recovery branch before retrying (if any attempts left) */
            if (branch && (attempts + 1 < max_retries)) {
                ESP_LOGW(TAG, "Step '%s' failed (attempt %d/%d) — running recovery branch '%s'",
                         step->id, attempts + 1, max_retries, branch->name);
                bool branch_ok = run_recovery_branch(branch);
                if (!branch_ok) {
                    ESP_LOGW(TAG, "Recovery branch '%s' failed — aborting", branch->name);
                    break;  /* Don't retry — fall through to failure handling */
                }
            }
        }

        /* ── Record result ──────────────────────────────────────────────── */

        if (result->step_result_count < JSON_RECIPE_STEPS_MAX) {
            result->step_results[result->step_result_count].passed = step_ok;
            result->step_result_count++;
        }

        /* Publish per-step result MQTT — after result recorded, before criticality handling */
        {
            uint32_t dur_ms = (result->step_result_count > 0)
                ? result->step_results[result->step_result_count - 1].duration_ms
                : 0;
            tc_mqtt_publish_step_result(step_num, step->id, step_status, step_ok, dur_ms);
        }

        result->total_count++;
        if (step_ok) {
            result->pass_count++;
        } else {
            /* Failure handling by criticality */
            switch (step->criticality) {
            case STEP_CRITICALITY_CRITICAL:
                /* Abort immediately — cut DUT power, mark remaining steps NOT_RUN */
                ESP_LOGE(TAG, "CRITICAL step '%s' failed — aborting + cutting DUT power", step->id);
                strlcpy(result->failed_step, step->id, sizeof(result->failed_step));
                g3_critical_abort();
                result->outcome = RECIPE_RESULT_ABORT;
                aborted = true;
                break;

            case STEP_CRITICALITY_REQUIRED:
                /* Record fail; continue for diagnostic data */
                ESP_LOGW(TAG, "REQUIRED step '%s' failed — continuing for diagnostics", step->id);
                if (!result->failed_step[0])
                    strlcpy(result->failed_step, step->id, sizeof(result->failed_step));
                any_required_fail = true;

                /* on_error=abort stops further steps (legacy behaviour) */
                if (!is_recovery && strcmp(step->on_error, "skip") != 0) {
                    aborted = true;
                }
                break;

            case STEP_CRITICALITY_OPTIONAL:
                /* Advisory only — log and continue; never fails recipe */
                ESP_LOGW(TAG, "OPTIONAL step '%s' failed (advisory)", step->id);
                break;
            }
        }

        if (aborted) break;

        /* Recipe timeout check */
        if (recipe->timeout_ms > 0) {
            uint32_t elapsed = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
            if (elapsed > recipe->timeout_ms) {
                ESP_LOGW(TAG, "Recipe timeout (%lu ms)", (unsigned long)recipe->timeout_ms);
                if (!result->failed_step[0])
                    strlcpy(result->failed_step, step->id, sizeof(result->failed_step));
                result->outcome = RECIPE_RESULT_TIMEOUT;
                goto done;
            }
        }
    }

done:
    result->duration_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);

    if (result->outcome != RECIPE_RESULT_TIMEOUT &&
        result->outcome != RECIPE_RESULT_ABORT) {
        result->outcome = (aborted || any_required_fail)
                          ? RECIPE_RESULT_FAIL : RECIPE_RESULT_PASS;
    }

    ESP_LOGI(TAG, "Recipe '%s' complete: %s  %d/%d  %lu ms",
             recipe->recipe_id,
             result->outcome == RECIPE_RESULT_PASS  ? "PASS"    :
             result->outcome == RECIPE_RESULT_FAIL  ? "FAIL"    :
             result->outcome == RECIPE_RESULT_ABORT ? "ABORT"   : "TIMEOUT",
             result->pass_count, result->total_count,
             (unsigned long)result->duration_ms);

    return 0;
}
