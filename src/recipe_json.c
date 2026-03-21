/* recipe_json.c — JSON recipe parser/serializer and NVS persistence.
 *
 * Uses ESP-IDF's built-in cJSON library. Recipes are stored as JSON blobs
 * in NVS namespace "recipes" with key = recipe_id.
 */

#include "recipe_json.h"
#include "recipe_primitives.h"
#include "recipe_engine.h"
#include "../storage/storage.h"
#include "cJSON.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_console.h"
#include "mbedtls/base64.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "recipe";

/* ── Parse ───────────────────────────────────────────────────────────────── */

int recipe_json_parse(const char *json_str, size_t len, json_recipe_t *out)
{
    if (!json_str || !out) return -1;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json_str, len);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed near: %s", cJSON_GetErrorPtr());
        return -1;
    }

    cJSON *item;
    if ((item = cJSON_GetObjectItem(root, "recipeId")))
        strlcpy(out->recipe_id, item->valuestring ? item->valuestring : "", sizeof(out->recipe_id));
    if ((item = cJSON_GetObjectItem(root, "recipeVersion")))
        strlcpy(out->recipe_version, item->valuestring ? item->valuestring : "", sizeof(out->recipe_version));
    if ((item = cJSON_GetObjectItem(root, "name")))
        strlcpy(out->name, item->valuestring ? item->valuestring : "", sizeof(out->name));
    if ((item = cJSON_GetObjectItem(root, "timeoutMs")))
        out->timeout_ms = (uint32_t)item->valuedouble;

    cJSON *steps = cJSON_GetObjectItem(root, "steps");
    if (cJSON_IsArray(steps)) {
        int count = cJSON_GetArraySize(steps);
        if (count > JSON_RECIPE_STEPS_MAX) count = JSON_RECIPE_STEPS_MAX;
        out->step_count = (uint8_t)count;

        for (int i = 0; i < count; i++) {
            cJSON *step = cJSON_GetArrayItem(steps, i);
            json_recipe_step_t *s = &out->steps[i];
            s->enabled = true;  /* default enabled unless explicitly disabled */

            if ((item = cJSON_GetObjectItem(step, "id")))
                strlcpy(s->id, item->valuestring ? item->valuestring : "", sizeof(s->id));
            if ((item = cJSON_GetObjectItem(step, "primitive")))
                strlcpy(s->primitive, item->valuestring ? item->valuestring : "", sizeof(s->primitive));
            if ((item = cJSON_GetObjectItem(step, "label")))
                strlcpy(s->label, item->valuestring ? item->valuestring : "", sizeof(s->label));
            if ((item = cJSON_GetObjectItem(step, "status_msg")))
                strlcpy(s->status_msg, item->valuestring ? item->valuestring : "", sizeof(s->status_msg));
            if ((item = cJSON_GetObjectItem(step, "onError")))
                strlcpy(s->on_error, item->valuestring ? item->valuestring : "", sizeof(s->on_error));
            if ((item = cJSON_GetObjectItem(step, "enabled")))
                s->enabled = cJSON_IsTrue(item);

            /* Default onError to "abort" if not specified */
            if (s->on_error[0] == '\0')
                strlcpy(s->on_error, "abort", sizeof(s->on_error));

            /* Criticality — default REQUIRED if absent or unrecognised */
            s->criticality = STEP_CRITICALITY_REQUIRED;
            if ((item = cJSON_GetObjectItem(step, "criticality")) && cJSON_IsString(item)) {
                if (strcmp(item->valuestring, "CRITICAL") == 0)
                    s->criticality = STEP_CRITICALITY_CRITICAL;
                else if (strcmp(item->valuestring, "OPTIONAL") == 0)
                    s->criticality = STEP_CRITICALITY_OPTIONAL;
            }

            /* params — non-owning pointer into root tree; root must outlive recipe */
            s->params = cJSON_GetObjectItem(step, "params");
        }
    }

    /* Recovery branches — optional "recoveryBranches" object at recipe root */
    cJSON *branches_obj = cJSON_GetObjectItem(root, "recoveryBranches");
    if (cJSON_IsObject(branches_obj)) {
        cJSON *branch = NULL;
        cJSON_ArrayForEach(branch, branches_obj) {
            if (out->recovery_branch_count >= JSON_RECOVERY_BRANCHES_MAX) break;
            json_recovery_branch_t *rb =
                &out->recovery_branches[out->recovery_branch_count];

            strlcpy(rb->name, branch->string ? branch->string : "", sizeof(rb->name));

            /* onComplete: "retry" (default) or "abort" */
            cJSON *oc = cJSON_GetObjectItem(branch, "onComplete");
            rb->retry_after = !(oc && cJSON_IsString(oc) &&
                                strcmp(oc->valuestring, "abort") == 0);

            cJSON *ma = cJSON_GetObjectItem(branch, "maxAttempts");
            rb->max_attempts = (ma && cJSON_IsNumber(ma))
                               ? (uint8_t)ma->valuedouble : 1;

            cJSON *bsteps = cJSON_GetObjectItem(branch, "steps");
            if (cJSON_IsArray(bsteps)) {
                int bcount = cJSON_GetArraySize(bsteps);
                if (bcount > JSON_RECOVERY_STEPS_MAX) bcount = JSON_RECOVERY_STEPS_MAX;
                rb->step_count = (uint8_t)bcount;
                for (int j = 0; j < bcount; j++) {
                    cJSON *bs = cJSON_GetArrayItem(bsteps, j);
                    json_recovery_step_t *rs = &rb->steps[j];
                    cJSON *bid  = cJSON_GetObjectItem(bs, "id");
                    cJSON *bprim = cJSON_GetObjectItem(bs, "primitive");
                    strlcpy(rs->id,        bid  && bid->valuestring  ? bid->valuestring  : "", sizeof(rs->id));
                    strlcpy(rs->primitive, bprim && bprim->valuestring ? bprim->valuestring : "", sizeof(rs->primitive));
                    rs->params = cJSON_GetObjectItem(bs, "params");
                }
            }
            out->recovery_branch_count++;
        }
    }

    /* Store owned root so callers can free via recipe_json_free() */
    out->_root = root;
    return 0;
}

/* ── Free ────────────────────────────────────────────────────────────────── */

void recipe_json_free(json_recipe_t *recipe)
{
    if (!recipe) return;
    if (recipe->_root) {
        cJSON_Delete(recipe->_root);
        recipe->_root = NULL;
    }
    /* Clear params pointers — they pointed into _root which is now freed */
    for (int i = 0; i < recipe->step_count; i++)
        recipe->steps[i].params = NULL;
}

/* ── Serialize ───────────────────────────────────────────────────────────── */

char *recipe_json_serialize(const json_recipe_t *recipe)
{
    if (!recipe) return NULL;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "recipeId", recipe->recipe_id);
    cJSON_AddStringToObject(root, "recipeVersion", recipe->recipe_version);
    cJSON_AddStringToObject(root, "name", recipe->name);
    cJSON_AddNumberToObject(root, "timeoutMs", recipe->timeout_ms);

    cJSON *steps = cJSON_AddArrayToObject(root, "steps");
    for (int i = 0; i < recipe->step_count; i++) {
        const json_recipe_step_t *s = &recipe->steps[i];
        cJSON *step = cJSON_CreateObject();
        cJSON_AddStringToObject(step, "id", s->id);
        cJSON_AddStringToObject(step, "primitive", s->primitive);
        if (s->label[0])      cJSON_AddStringToObject(step, "label",      s->label);
        if (s->status_msg[0]) cJSON_AddStringToObject(step, "status_msg", s->status_msg);
        cJSON_AddStringToObject(step, "onError", s->on_error);
        cJSON_AddBoolToObject(step, "enabled", s->enabled);
        if (s->params) {
            cJSON *p = cJSON_Duplicate(s->params, true);
            if (p) cJSON_AddItemToObject(step, "params", p);
        }
        cJSON_AddItemToArray(steps, step);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;  /* caller must free */
}

/* ── Bridge: hardcoded → JSON ────────────────────────────────────────────── */

int recipe_json_from_hardcoded(json_recipe_t *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    strlcpy(out->recipe_id, "fixture-selftest", sizeof(out->recipe_id));
    strlcpy(out->recipe_version, "1.0.0", sizeof(out->recipe_version));
    strlcpy(out->name, "G3 Fixture Selftest", sizeof(out->name));
    out->timeout_ms = 60000;

    /* Build steps from the dispatch table */
    int prim_count = 0;
    const primitive_entry_t *table = recipe_primitives_table(&prim_count);

    out->step_count = 0;
    for (int i = 0; i < prim_count && out->step_count < JSON_RECIPE_STEPS_MAX; i++) {
        json_recipe_step_t *s = &out->steps[out->step_count];
        strlcpy(s->id, table[i].id, sizeof(s->id));
        strlcpy(s->primitive, table[i].id, sizeof(s->primitive));
        strlcpy(s->on_error, "abort", sizeof(s->on_error));
        /* First 6 are enabled (carrier checks), rest disabled (DUT pogo-dependent) */
        s->enabled = (i < 6);
        out->step_count++;
    }

    return 0;
}

/* ── Recipe persistence (LittleFS via storage.h) ─────────────────────────── */

int recipe_json_store_nvs(const char *recipe_id, const char *json_str, size_t len)
{
    int rc = Storage_Write(STORAGE_DOMAIN_RECIPES, recipe_id,
                           (const uint8_t *)json_str, len, false);
    if (rc == 0)
        ESP_LOGI(TAG, "Stored recipe '%s' (%d bytes)", recipe_id, (int)len);
    else
        ESP_LOGW(TAG, "Failed to store recipe '%s'", recipe_id);
    return rc;
}

char *recipe_json_load_nvs(const char *recipe_id)
{
    int32_t sz = Storage_GetSize(STORAGE_DOMAIN_RECIPES, recipe_id);
    if (sz <= 0) return NULL;

    char *buf = malloc((size_t)sz + 1);
    if (!buf) return NULL;

    int32_t n = Storage_Read(STORAGE_DOMAIN_RECIPES, recipe_id,
                             (uint8_t *)buf, (size_t)sz);
    if (n <= 0) {
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    ESP_LOGI(TAG, "Loaded recipe '%s' (%ld bytes)", recipe_id, (long)n);
    return buf;
}

int recipe_json_delete_nvs(const char *recipe_id)
{
    return Storage_Delete(STORAGE_DOMAIN_RECIPES, recipe_id);
}

/* ── Console commands ────────────────────────────────────────────────────── */

static int do_recipe(int argc, char **argv)
{
    if (argc < 2) goto usage;

    if (strcmp(argv[1], "show") == 0) {
        json_recipe_t *recipe = malloc(sizeof(json_recipe_t));
        if (!recipe) { printf("malloc failed\n"); return 1; }
        recipe_json_from_hardcoded(recipe);
        char *json = recipe_json_serialize(recipe);
        free(recipe);
        if (json) {
            printf("%s\n", json);
            free(json);
        } else {
            printf("Serialize failed\n");
        }
        return 0;
    }

    if (strcmp(argv[1], "store") == 0) {
        const char *id = (argc >= 3) ? argv[2] : "default";
        json_recipe_t *recipe = malloc(sizeof(json_recipe_t));
        if (!recipe) { printf("malloc failed\n"); return 1; }
        recipe_json_from_hardcoded(recipe);
        strlcpy(recipe->recipe_id, id, sizeof(recipe->recipe_id));
        char *json = recipe_json_serialize(recipe);
        free(recipe);
        if (!json) { printf("Serialize failed\n"); return 1; }
        int rc = recipe_json_store_nvs(id, json, strlen(json));
        printf("%s: %s\n", id, rc == 0 ? "stored" : "FAILED");
        free(json);
        return rc;
    }

    if (strcmp(argv[1], "load") == 0) {
        const char *id = (argc >= 3) ? argv[2] : "default";
        char *json = recipe_json_load_nvs(id);
        if (!json) { printf("Recipe '%s' not found\n", id); return 1; }

        json_recipe_t *recipe = malloc(sizeof(json_recipe_t));
        if (!recipe) { free(json); printf("malloc failed\n"); return 1; }
        int rc = recipe_json_parse(json, strlen(json), recipe);
        if (rc == 0) {
            printf("Recipe: %s v%s (%s)\n", recipe->recipe_id, recipe->recipe_version, recipe->name);
            printf("Steps: %d  timeout: %lu ms\n", recipe->step_count, (unsigned long)recipe->timeout_ms);
            for (int i = 0; i < recipe->step_count; i++) {
                json_recipe_step_t *s = &recipe->steps[i];
                printf("  %2d. [%s] %-20s  prim=%-16s  onErr=%s\n",
                       i + 1, s->enabled ? "ON " : "off", s->id, s->primitive, s->on_error);
            }
        } else {
            printf("Parse failed\n");
        }
        recipe_json_free(recipe);
        free(recipe);
        free(json);
        return rc;
    }

    if (strcmp(argv[1], "list") == 0) {
        const char *keys[20];
        int32_t count = Storage_ListKeys(STORAGE_DOMAIN_RECIPES, keys, 20);
        if (count <= 0) {
            printf("No recipes stored\n");
            return 0;
        }
        printf("Stored recipes (%ld):\n", (long)count);
        for (int i = 0; i < count; i++) {
            int32_t sz = Storage_GetSize(STORAGE_DOMAIN_RECIPES, keys[i]);
            printf("  %s  (%ld bytes)\n", keys[i], (long)sz);
            free((void *)keys[i]);
        }
        return 0;
    }

    /* recipe set-b64 <id> <base64-encoded-json>
     * Upload a recipe JSON to storage without firmware rebuild.
     * Use scripts/upload_recipe.py to generate the base64 argument. */
    if (strcmp(argv[1], "set-b64") == 0) {
        if (argc < 4) { printf("Usage: recipe set-b64 <id> <base64-json>\n"); return 1; }
        const char *id      = argv[2];
        const char *b64_in  = argv[3];
        size_t      b64_len = strlen(b64_in);

        /* Decode: output is at most 3/4 of input */
        size_t out_len = 0;
        size_t buf_sz  = (b64_len / 4) * 3 + 4;
        char  *json    = malloc(buf_sz);
        if (!json) { printf("malloc failed\n"); return 1; }

        int rc = mbedtls_base64_decode((unsigned char *)json, buf_sz, &out_len,
                                       (const unsigned char *)b64_in, b64_len);
        if (rc != 0) {
            free(json);
            printf("Base64 decode failed (rc=%d)\n", rc);
            return 1;
        }
        json[out_len] = '\0';

        /* Quick JSON validation */
        cJSON *root = cJSON_ParseWithLength(json, out_len);
        if (!root) {
            free(json);
            printf("JSON parse failed near: %s\n", cJSON_GetErrorPtr());
            return 1;
        }
        cJSON_Delete(root);

        int store_rc = recipe_json_store_nvs(id, json, out_len);
        free(json);
        printf("%s: %s (%d bytes)\n", id, store_rc == 0 ? "stored" : "FAILED", (int)out_len);
        return store_rc;
    }

    if (strcmp(argv[1], "delete") == 0) {
        if (argc < 3) { printf("Usage: recipe delete <id>\n"); return 1; }
        int rc = recipe_json_delete_nvs(argv[2]);
        printf("%s: %s\n", argv[2], rc == 0 ? "deleted" : "NOT FOUND");
        return rc;
    }

    if (strcmp(argv[1], "run") == 0) {
        const char *id = (argc >= 3) ? argv[2] : "default";

        /* Load recipe JSON from NVS */
        char *json = recipe_json_load_nvs(id);
        if (!json) {
            /* If no stored recipe, use hardcoded */
            printf("Recipe '%s' not in NVS — using hardcoded\n", id);
            json_recipe_t *recipe = malloc(sizeof(json_recipe_t));
            if (!recipe) { printf("malloc failed\n"); return 1; }
            recipe_json_from_hardcoded(recipe);

            recipe_run_result_t *result = malloc(sizeof(recipe_run_result_t));
            if (!result) { free(recipe); printf("malloc failed\n"); return 1; }

            recipe_engine_run(recipe, result);

            printf("\n=== Recipe: %s  outcome=%s  %d/%d  %lu ms ===\n",
                   recipe->recipe_id,
                   result->outcome == RECIPE_RESULT_PASS ? "PASS" :
                   result->outcome == RECIPE_RESULT_FAIL ? "FAIL" : "TIMEOUT",
                   result->pass_count, result->total_count,
                   (unsigned long)result->duration_ms);
            if (result->failed_step[0])
                printf("Failed step: %s\n", result->failed_step);

            int rc_hc = (result->outcome == RECIPE_RESULT_PASS) ? 0 : 1;
            free(result);
            free(recipe);
            return rc_hc;
        }

        /* Parse stored JSON */
        json_recipe_t *recipe = malloc(sizeof(json_recipe_t));
        if (!recipe) { free(json); printf("malloc failed\n"); return 1; }
        if (recipe_json_parse(json, strlen(json), recipe) != 0) {
            free(json); free(recipe);
            printf("Parse failed\n");
            return 1;
        }
        free(json);  /* raw JSON string no longer needed — tree is in recipe->_root */

        recipe_run_result_t *result = malloc(sizeof(recipe_run_result_t));
        if (!result) { recipe_json_free(recipe); free(recipe); printf("malloc failed\n"); return 1; }

        recipe_engine_run(recipe, result);

        printf("\n=== Recipe: %s  outcome=%s  %d/%d  %lu ms ===\n",
               recipe->recipe_id,
               result->outcome == RECIPE_RESULT_PASS ? "PASS" :
               result->outcome == RECIPE_RESULT_FAIL ? "FAIL" : "TIMEOUT",
               result->pass_count, result->total_count,
               (unsigned long)result->duration_ms);
        if (result->failed_step[0])
            printf("Failed step: %s\n", result->failed_step);

        int rc = (result->outcome == RECIPE_RESULT_PASS) ? 0 : 1;
        free(result);
        recipe_json_free(recipe);
        free(recipe);
        return rc;
    }

usage:
    printf("Usage:\n"
           "  recipe show              show hardcoded recipe as JSON\n"
           "  recipe store [id]        store hardcoded recipe to flash\n"
           "  recipe set-b64 <id> <b64> upload base64-encoded JSON to flash\n"
           "  recipe load [id]         load recipe from flash and print\n"
           "  recipe list              list all stored recipes\n"
           "  recipe delete <id>       delete recipe from flash\n"
           "  recipe run [id]          run recipe (from flash or hardcoded fallback)\n");
    return 1;
}

void register_recipe_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "recipe",
        .help    = "Recipe management: recipe <show|store|load>",
        .hint    = NULL,
        .func    = &do_recipe,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
