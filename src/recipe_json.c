/* recipe_json.c — JSON recipe parser/serializer and NVS persistence.
 *
 * Uses ESP-IDF's built-in cJSON library. Recipes are stored as JSON blobs
 * in NVS namespace "recipes" with key = recipe_id.
 */

#include "recipe_json.h"
#include "recipe_primitives.h"
#include "cJSON.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_console.h"
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
            if ((item = cJSON_GetObjectItem(step, "onError")))
                strlcpy(s->on_error, item->valuestring ? item->valuestring : "", sizeof(s->on_error));
            if ((item = cJSON_GetObjectItem(step, "enabled")))
                s->enabled = cJSON_IsTrue(item);

            /* Default onError to "abort" if not specified */
            if (s->on_error[0] == '\0')
                strlcpy(s->on_error, "abort", sizeof(s->on_error));
        }
    }

    cJSON_Delete(root);
    return 0;
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
        if (s->label[0]) cJSON_AddStringToObject(step, "label", s->label);
        cJSON_AddStringToObject(step, "onError", s->on_error);
        cJSON_AddBoolToObject(step, "enabled", s->enabled);
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

/* ── NVS persistence ─────────────────────────────────────────────────────── */

#define NVS_NAMESPACE "recipes"

int recipe_json_store_nvs(const char *recipe_id, const char *json_str, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return -1;
    }
    err = nvs_set_blob(h, recipe_id, json_str, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS write failed: %s", esp_err_to_name(err));
        return -1;
    }
    ESP_LOGI(TAG, "Stored recipe '%s' (%d bytes)", recipe_id, (int)len);
    return 0;
}

char *recipe_json_load_nvs(const char *recipe_id)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return NULL;

    size_t len = 0;
    if (nvs_get_blob(h, recipe_id, NULL, &len) != ESP_OK || len == 0) {
        nvs_close(h);
        return NULL;
    }

    char *buf = malloc(len + 1);
    if (!buf) { nvs_close(h); return NULL; }

    if (nvs_get_blob(h, recipe_id, buf, &len) != ESP_OK) {
        free(buf);
        nvs_close(h);
        return NULL;
    }
    nvs_close(h);
    buf[len] = '\0';
    ESP_LOGI(TAG, "Loaded recipe '%s' (%d bytes)", recipe_id, (int)len);
    return buf;
}

int recipe_json_delete_nvs(const char *recipe_id)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return -1;
    esp_err_t err = nvs_erase_key(h, recipe_id);
    nvs_commit(h);
    nvs_close(h);
    return (err == ESP_OK) ? 0 : -1;
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
        free(recipe);
        free(json);
        return rc;
    }

usage:
    printf("Usage:\n"
           "  recipe show              show hardcoded recipe as JSON\n"
           "  recipe store [id]        store hardcoded recipe to NVS (default: 'default')\n"
           "  recipe load [id]         load recipe from NVS and print\n");
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
