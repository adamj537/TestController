/* recipe_task.c — FreeRTOS task bridge between the state machine and the
 *                  recipe engine.
 *
 * tc_sm_spawn_recipe_task() is declared extern in tc_statemachine.c
 * (common submodule cannot depend on src/ directly).
 *
 * Flow:
 *   tc_sm_spawn_recipe_task(recipe_id)
 *     → spawn recipe_run_task
 *         → load active recipe from storage (ID from arg or NVS recipes/active)
 *         → recipe_engine_run()
 *         → tc_sm_recipe_done()      ← back into state machine
 */

#include "recipe_engine.h"
#include "recipe_json.h"
#include "tc_statemachine.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "recipe_task";

/* ── Task argument ────────────────────────────────────────────────────────── */

typedef struct {
    char recipe_id[64];   /* empty → read from NVS recipes/active */
} recipe_task_arg_t;

/* ── Task ─────────────────────────────────────────────────────────────────── */

static void recipe_run_task(void *pvarg)
{
    recipe_task_arg_t *arg = (recipe_task_arg_t *)pvarg;
    char recipe_id[64];
    strlcpy(recipe_id, arg->recipe_id, sizeof(recipe_id));
    free(arg);

    /* If no ID provided, load active recipe ID from NVS */
    if (!recipe_id[0]) {
        nvs_handle_t h;
        if (nvs_open("recipes", NVS_READONLY, &h) == ESP_OK) {
            size_t len = sizeof(recipe_id);
            nvs_get_str(h, "active", recipe_id, &len);
            nvs_close(h);
        }
        if (!recipe_id[0]) {
            strlcpy(recipe_id, "default", sizeof(recipe_id));
            ESP_LOGW(TAG, "No active recipe in NVS — falling back to 'default'");
        } else {
            ESP_LOGI(TAG, "Active recipe from NVS: %s", recipe_id);
        }
    }

    /* Load recipe JSON from storage */
    char *json = recipe_json_load_nvs(recipe_id);
    json_recipe_t *recipe = malloc(sizeof(json_recipe_t));
    recipe_run_result_t *result = malloc(sizeof(recipe_run_result_t));

    if (!recipe || !result) {
        ESP_LOGE(TAG, "malloc failed");
        free(json);
        free(recipe);
        free(result);
        tc_sm_recipe_done(2 /* ABORT */, "malloc_failed", 0, 0, 0);
        vTaskDelete(NULL);
        return;
    }

    int parse_ok = 0;
    if (json) {
        parse_ok = (recipe_json_parse(json, strlen(json), recipe) == 0);
        free(json);
    }

    if (!parse_ok) {
        /* Fall back to hardcoded recipe if stored recipe missing or corrupt */
        ESP_LOGW(TAG, "Recipe '%s' not found or corrupt — using hardcoded", recipe_id);
        recipe_json_from_hardcoded(recipe);
    }

    ESP_LOGI(TAG, "Running recipe: %s v%s (%d steps)",
             recipe->recipe_id, recipe->recipe_version, recipe->step_count);

    recipe_engine_run(recipe, result);

    int outcome    = (int)result->outcome;
    char step[JSON_STEP_ID_MAX];
    strlcpy(step, result->failed_step, sizeof(step));
    uint32_t dur   = result->duration_ms;
    int pass_count = result->pass_count;
    int total      = result->total_count;

    recipe_json_free(recipe);
    free(recipe);
    free(result);

    tc_sm_recipe_done(outcome, step, dur, pass_count, total);
    vTaskDelete(NULL);
}

/* ── Public — called from tc_statemachine.c (via extern declaration) ──────── */

void tc_sm_spawn_recipe_task(const char *recipe_id)
{
    recipe_task_arg_t *arg = malloc(sizeof(*arg));
    if (!arg) {
        ESP_LOGE(TAG, "spawn_recipe_task: malloc failed");
        tc_sm_recipe_done(2 /* ABORT */, "malloc_failed", 0, 0, 0);
        return;
    }
    if (recipe_id && recipe_id[0]) {
        strlcpy(arg->recipe_id, recipe_id, sizeof(arg->recipe_id));
    } else {
        arg->recipe_id[0] = '\0';
    }
    /* 16 KB stack: recipe primitives (SWD, I2C, UART) have deep call chains */
    xTaskCreate(recipe_run_task, "recipe_run", 16384, arg, 4, NULL);
}
