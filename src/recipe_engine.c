/* recipe_engine.c — Execute a json_recipe_t via the primitive dispatch table.
 *
 * Iterates recipe steps, resolves each primitive by name, invokes it,
 * captures results from the selftest check buffer, and publishes MQTT
 * progress per step.
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

int recipe_engine_run(const json_recipe_t *recipe, recipe_run_result_t *result)
{
    if (!recipe || !result) return -1;
    memset(result, 0, sizeof(*result));

    int64_t start_us = esp_timer_get_time();

    /* Reset the selftest check buffer */
    selftest_reset_checks();

    /* Count enabled steps for progress reporting */
    int enabled_total = 0;
    for (int i = 0; i < recipe->step_count; i++)
        if (recipe->steps[i].enabled) enabled_total++;

    int step_num = 0;
    bool aborted = false;

    for (int i = 0; i < recipe->step_count && !aborted; i++) {
        const json_recipe_step_t *step = &recipe->steps[i];

        if (!step->enabled) {
            printf("[SKIP] %s\n", step->id);
            continue;
        }

        /* Resolve primitive */
        primitive_fn_t fn = recipe_primitives_lookup(step->primitive);
        if (!fn) {
            printf("[SKIP] %s (primitive '%s' not found)\n", step->id, step->primitive);
            continue;
        }

        step_num++;

        /* Publish progress */
        tc_mqtt_publish_test_progress(true, step_num, enabled_total,
                                       step->label[0] ? step->label : step->id);

        /* Snapshot check buffer before step */
        int ncheck_before = selftest_get_ncheck();
        int pass_before   = selftest_get_pass();
        int total_before  = selftest_get_total();
        int64_t step_start = esp_timer_get_time();

        /* Execute primitive with per-step params (NULL if not specified) */
        fn(step->params);

        /* Capture step results from check buffer delta */
        int64_t step_end = esp_timer_get_time();
        int ncheck_after = selftest_get_ncheck();
        int pass_after   = selftest_get_pass();
        int total_after  = selftest_get_total();

        int step_checks = total_after - total_before;
        int step_passes = pass_after - pass_before;
        bool step_ok = (step_checks > 0) ? (step_passes == step_checks) : true;

        /* Record step result */
        if (result->step_result_count < JSON_RECIPE_STEPS_MAX) {
            step_result_t *sr = &result->step_results[result->step_result_count];
            strlcpy(sr->step_id, step->id, sizeof(sr->step_id));
            sr->passed = step_ok;
            sr->duration_ms = (uint32_t)((step_end - step_start) / 1000);

            /* Grab last measured value from check buffer if available */
            if (ncheck_after > ncheck_before) {
                const tc_mqtt_check_t *checks = selftest_get_checks();
                const tc_mqtt_check_t *last = &checks[ncheck_after - 1];
                sr->has_value = last->has_value;
                sr->measured_value = last->value;
            }
            result->step_result_count++;
        }

        result->total_count++;
        if (step_ok) {
            result->pass_count++;
        } else {
            /* Handle onError */
            if (strcmp(step->on_error, "skip") == 0) {
                /* Skip: log and continue */
                ESP_LOGW(TAG, "Step '%s' failed but on_error=skip — continuing", step->id);
            } else {
                /* abort (default) */
                strlcpy(result->failed_step, step->id, sizeof(result->failed_step));
                aborted = true;
            }
        }

        /* Check recipe timeout */
        if (recipe->timeout_ms > 0) {
            uint32_t elapsed = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
            if (elapsed > recipe->timeout_ms) {
                ESP_LOGW(TAG, "Recipe timeout (%lu ms)", (unsigned long)recipe->timeout_ms);
                strlcpy(result->failed_step, step->id, sizeof(result->failed_step));
                result->outcome = RECIPE_RESULT_TIMEOUT;
                break;
            }
        }
    }

    result->duration_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);

    if (result->outcome != RECIPE_RESULT_TIMEOUT) {
        result->outcome = aborted ? RECIPE_RESULT_FAIL : RECIPE_RESULT_PASS;
    }

    ESP_LOGI(TAG, "Recipe '%s' complete: %s  %d/%d  %lu ms",
             recipe->recipe_id,
             result->outcome == RECIPE_RESULT_PASS ? "PASS" :
             result->outcome == RECIPE_RESULT_FAIL ? "FAIL" : "TIMEOUT",
             result->pass_count, result->total_count,
             (unsigned long)result->duration_ms);

    return 0;
}
