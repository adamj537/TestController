#pragma once

#include "recipe_json.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RECIPE_RESULT_PASS,
    RECIPE_RESULT_FAIL,
    RECIPE_RESULT_ABORT,
    RECIPE_RESULT_TIMEOUT
} recipe_result_t;

typedef struct {
    char     step_id[JSON_STEP_ID_MAX];
    bool     passed;
    int      measured_value;
    bool     has_value;
    uint32_t duration_ms;
} step_result_t;

typedef struct {
    recipe_result_t outcome;
    char            failed_step[JSON_STEP_ID_MAX];
    uint32_t        duration_ms;
    int             pass_count;
    int             total_count;
    uint8_t         step_result_count;
    step_result_t   step_results[JSON_RECIPE_STEPS_MAX];
} recipe_run_result_t;

/* Execute a json_recipe_t. Blocks until complete.
 * Publishes MQTT test_progress per step.
 * Returns 0 on execution success (even if recipe outcome is FAIL). */
int recipe_engine_run(const json_recipe_t *recipe, recipe_run_result_t *result);

#ifdef __cplusplus
}
#endif
