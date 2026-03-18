#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── JSON recipe data structures ─────────────────────────────────────────── */

#define JSON_RECIPE_ID_MAX       64
#define JSON_RECIPE_STEPS_MAX    50
#define JSON_STEP_ID_MAX         32
#define JSON_STEP_LABEL_MAX      48
#define JSON_STEP_PRIMITIVE_MAX  32
#define JSON_STEP_ON_ERROR_MAX   32

typedef struct {
    char id[JSON_STEP_ID_MAX];
    char primitive[JSON_STEP_PRIMITIVE_MAX];
    char label[JSON_STEP_LABEL_MAX];
    char on_error[JSON_STEP_ON_ERROR_MAX];   /* "abort" | "skip" | "recovery:<name>" */
    bool enabled;
} json_recipe_step_t;

typedef struct {
    char recipe_id[JSON_RECIPE_ID_MAX];
    char recipe_version[16];
    char name[64];
    uint32_t timeout_ms;
    uint8_t  step_count;
    json_recipe_step_t steps[JSON_RECIPE_STEPS_MAX];
} json_recipe_t;

/* ── Parse / Serialize ───────────────────────────────────────────────────── */

/* Parse a JSON string into a json_recipe_t.
 * Returns 0 on success, -1 on parse error. */
int recipe_json_parse(const char *json_str, size_t len, json_recipe_t *out);

/* Serialize a json_recipe_t to a JSON string.
 * Returns heap-allocated buffer (caller must free). NULL on error. */
char *recipe_json_serialize(const json_recipe_t *recipe);

/* ── Bridge: hardcoded → JSON ────────────────────────────────────────────── */

/* Generate a json_recipe_t from the current hardcoded s_fixture_recipe[].
 * Returns 0 on success. */
int recipe_json_from_hardcoded(json_recipe_t *out);

/* ── NVS persistence ─────────────────────────────────────────────────────── */

/* Store recipe JSON string to NVS. Key = recipe_id. */
int recipe_json_store_nvs(const char *recipe_id, const char *json_str, size_t len);

/* Load recipe JSON string from NVS. Returns heap-allocated string (caller must free).
 * Returns NULL if not found or error. */
char *recipe_json_load_nvs(const char *recipe_id);

/* Delete recipe from NVS. Returns 0 on success, -1 on error. */
int recipe_json_delete_nvs(const char *recipe_id);

/* Register "recipe" console command. */
void register_recipe_commands(void);

#ifdef __cplusplus
}
#endif
