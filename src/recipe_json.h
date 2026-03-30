#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── JSON recipe data structures ─────────────────────────────────────────── */

#define JSON_RECIPE_ID_MAX             64
#define JSON_RECIPE_STEPS_MAX          140
#define JSON_STEP_ID_MAX               32
#define JSON_STEP_LABEL_MAX            48
#define JSON_STEP_STATUS_MAX           64
#define JSON_STEP_PRIMITIVE_MAX        32
#define JSON_STEP_ON_ERROR_MAX         32
#define JSON_RECOVERY_BRANCH_NAME_MAX  32
#define JSON_RECOVERY_BRANCHES_MAX     8
#define JSON_RECOVERY_STEPS_MAX        8

/* Step criticality — controls engine behaviour on failure. */
typedef enum {
    STEP_CRITICALITY_REQUIRED = 0,  /* Default. Record fail; continue for diagnostics. */
    STEP_CRITICALITY_CRITICAL,      /* Abort immediately; cut DUT power within 100 ms. */
    STEP_CRITICALITY_OPTIONAL,      /* Advisory only; never affects pass/fail outcome. */
} step_criticality_t;

/* A single step inside a recovery branch (no onError/criticality — any failure aborts). */
typedef struct {
    char    id[JSON_STEP_ID_MAX];
    char    primitive[JSON_STEP_PRIMITIVE_MAX];
    cJSON  *params;   /* Non-owning pointer into recipe's _root cJSON tree. NULL = no params. */
} json_recovery_step_t;

/* A named recovery branch invoked by "recovery:<name>" in step on_error. */
typedef struct {
    char                  name[JSON_RECOVERY_BRANCH_NAME_MAX];
    uint8_t               step_count;
    json_recovery_step_t  steps[JSON_RECOVERY_STEPS_MAX];
    bool                  retry_after;   /* true = retry failed step; false = abort after branch */
    uint8_t               max_attempts;  /* max times branch may be invoked per step (default 1) */
} json_recovery_branch_t;

typedef struct {
    char                id[JSON_STEP_ID_MAX];
    char                primitive[JSON_STEP_PRIMITIVE_MAX];
    char                label[JSON_STEP_LABEL_MAX];
    char                status_msg[JSON_STEP_STATUS_MAX];   /* optional MQTT status override; defaults to label */
    char                on_error[JSON_STEP_ON_ERROR_MAX];   /* "abort" | "skip" | "recovery:<name>" */
    step_criticality_t  criticality;
    bool                enabled;
    bool                requires_pass;  /* if true, step is skipped when any REQUIRED step has failed */
    cJSON              *params;   /* Non-owning pointer into recipe's _root cJSON tree. NULL = no params. */
} json_recipe_step_t;

typedef struct {
    char    recipe_id[JSON_RECIPE_ID_MAX];
    char    recipe_version[16];
    char    name[64];
    uint32_t timeout_ms;
    uint8_t  step_count;
    json_recipe_step_t steps[JSON_RECIPE_STEPS_MAX];
    uint8_t  recovery_branch_count;
    json_recovery_branch_t recovery_branches[JSON_RECOVERY_BRANCHES_MAX];
    cJSON  *_root;    /* Owned cJSON parse tree. Free with recipe_json_free(). NULL if not parsed. */
} json_recipe_t;

/* ── Parse / Serialize ───────────────────────────────────────────────────── */

/* Parse a JSON string into a json_recipe_t.
 * Returns 0 on success, -1 on parse error.
 * On success, recipe->_root owns the cJSON tree. Call recipe_json_free() when done. */
int recipe_json_parse(const char *json_str, size_t len, json_recipe_t *out);

/* Free the cJSON parse tree owned by a recipe (recipe->_root).
 * Safe to call on recipes built by recipe_json_from_hardcoded() (_root == NULL).
 * Does NOT free the json_recipe_t struct itself. */
void recipe_json_free(json_recipe_t *recipe);

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
