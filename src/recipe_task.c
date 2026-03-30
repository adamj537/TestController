/* recipe_task.c — FreeRTOS task bridge between the state machine and the
 *                  recipe engine.
 *
 * tc_sm_spawn_recipe_task() is declared extern in tc_statemachine.c
 * (common submodule cannot depend on src/ directly).
 *
 * Flow:
 *   tc_sm_spawn_recipe_task(recipe_id)
 *     → spawn recipe_run_task + dut_watchdog_task (in parallel)
 *         recipe_run_task:
 *           → load active recipe from storage (ID from arg or NVS recipes/active)
 *           → recipe_engine_run()
 *           → tc_sm_recipe_done()      ← back into state machine
 *         dut_watchdog_task (FLT-011):
 *           → polls INA219 current every 300 ms while TESTING
 *           → on 2 consecutive near-zero readings: checks dut_detect_present()
 *           → DUT absent → tc_sm_cmd_dut_removed()
 *           → exits when state leaves TC_SM_TESTING
 */

#include "recipe_engine.h"
#include "recipe_json.h"
#include "tc_statemachine.h"
#include "dut_detect.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "recipe_task";

/* Declared in g3_primitives.c — reads VDUT1 INA219 current. */
extern bool g3_ina219_read_current_ma(int *ma_out);

/* Current below this threshold triggers a DUT-absent confirmation check.
 * Set conservatively below the PRD pre-gate minimum (5 mA) to avoid false
 * triggers on lightly-loaded test steps.  Configurable if needed. */
#define WATCHDOG_NEAR_ZERO_MA   2

/* ── Task argument ────────────────────────────────────────────────────────── */

typedef struct {
    char recipe_id[64];   /* empty → read from NVS recipes/active */
} recipe_task_arg_t;

/* Static recipe/result — only one recipe runs at a time.  Avoids heap
 * fragmentation failures: json_recipe_t is ~26 KB, too large for a
 * contiguous malloc after many precheck cycles. */
static json_recipe_t      s_recipe;
static recipe_run_result_t s_result;

/* Shared 16 KB worker stack — defined in tc_statemachine.c, shared with
 * flash_dut_task.  State machine guarantees mutual exclusion. */
#define RECIPE_STACK_WORDS  (16384 / sizeof(StackType_t))
extern StackType_t tc_sm_worker_stack[];
static StaticTask_t  s_recipe_tcb;

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
            ESP_LOGE(TAG, "No active recipe configured — abort");
            tc_sm_recipe_done(2 /* ABORT */, "recipe_not_configured", 0, 0, 0);
            vTaskDelete(NULL);
            return;   /* unreachable — keeps static analysers happy */
        }
        ESP_LOGI(TAG, "Active recipe from NVS: %s", recipe_id);
    }

    /* Load recipe JSON from storage */
    char *json = recipe_json_load_nvs(recipe_id);
    json_recipe_t *recipe = &s_recipe;
    recipe_run_result_t *result = &s_result;

    int parse_ok = 0;
    if (json) {
        parse_ok = (recipe_json_parse(json, strlen(json), recipe) == 0);
        free(json);
    }

    if (!parse_ok) {
        ESP_LOGE(TAG, "Recipe '%s' not found or corrupt — abort", recipe_id);
        tc_sm_recipe_done(2 /* ABORT */, "recipe_not_found", 0, 0, 0);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Running recipe: %s v%s (%d steps)",
             recipe->recipe_id, recipe->recipe_version, recipe->step_count);

    tc_sm_set_recipe_version(recipe->recipe_version);

    recipe_engine_run(recipe, result);

    int outcome    = (int)result->outcome;
    char step[JSON_STEP_ID_MAX];
    strlcpy(step, result->failed_step, sizeof(step));
    uint32_t dur   = result->duration_ms;
    int pass_count = result->pass_count;
    int total      = result->total_count;

    recipe_json_free(recipe);

    tc_sm_recipe_done(outcome, step, dur, pass_count, total);
    vTaskDelete(NULL);
}

/* ── DUT removal watchdog (FLT-011) ──────────────────────────────────────── *
 *
 * Runs in parallel with recipe_run_task.  Monitors VDUT1 INA219 current;
 * on two consecutive near-zero readings confirms via dut_detect_present().
 * Exits cleanly when state machine leaves TC_SM_TESTING (recipe done or
 * already faulted by another path).
 */
static void dut_watchdog_task(void *pvarg)
{
    (void)pvarg;
    int consecutive_zero = 0;

    while (tc_sm_state() == TC_SM_TESTING) {
        vTaskDelay(pdMS_TO_TICKS(300));

        if (tc_sm_state() != TC_SM_TESTING) break;

        int current_ma = 0;
        if (!g3_ina219_read_current_ma(&current_ma)) {
            /* I2C error — reset counter, don't false-trigger */
            consecutive_zero = 0;
            continue;
        }

        if (current_ma < WATCHDOG_NEAR_ZERO_MA) {
            consecutive_zero++;
            if (consecutive_zero >= 2) {
                /* Current has been near-zero for ≥600 ms — confirm DUT absent */
                if (!dut_detect_present()) {
                    ESP_LOGE("dut_wdog", "FLT-011: current=%d mA + DUT absent — triggering E-STOP",
                             current_ma);
                    tc_sm_cmd_dut_removed();
                    break;
                }
                /* Current low but DUT still detected — reset and keep watching
                 * (could be a test step with outputs off; not a removal event) */
                consecutive_zero = 0;
            }
        } else {
            consecutive_zero = 0;
        }
    }

    vTaskDelete(NULL);
}

/* ── Public — called from tc_statemachine.c (via extern declaration) ──────── *
 * Returns true on success.  On failure the caller (which holds SM_LOCK) must
 * handle the SM transition — calling tc_sm_recipe_done() from here would
 * deadlock because tc_sm_recipe_done() also takes SM_LOCK.                   */

bool tc_sm_spawn_recipe_task(const char *recipe_id)
{
    recipe_task_arg_t *arg = malloc(sizeof(*arg));
    if (!arg) {
        ESP_LOGE(TAG, "spawn_recipe_task: malloc failed (free heap=%lu, internal=%lu)",
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        return false;
    }
    if (recipe_id && recipe_id[0]) {
        strlcpy(arg->recipe_id, recipe_id, sizeof(arg->recipe_id));
    } else {
        arg->recipe_id[0] = '\0';
    }
    /* Shared 16 KB static worker stack — see tc_sm_worker_stack in tc_statemachine.c. */
    TaskHandle_t h = xTaskCreateStatic(recipe_run_task, "recipe_run",
                                       RECIPE_STACK_WORDS, arg, 4,
                                       tc_sm_worker_stack, &s_recipe_tcb);
    if (!h) {
        ESP_LOGE(TAG, "spawn_recipe_task: xTaskCreateStatic failed");
        free(arg);
        return false;
    }
    /* 2 KB stack: watchdog only reads INA219 + checks DUT detect cached state */
    xTaskCreate(dut_watchdog_task, "dut_wdog", 2048, NULL, 4, NULL);
    return true;
}
