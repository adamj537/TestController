/* tc_config.c — Device configuration: JSON blob in LittleFS, live cJSON tree.
 *
 * Single storage key: STORAGE_DOMAIN_CONFIGURATION / "device"
 *
 * Dot-path accessors traverse nested objects: "vdut.slope_mv_per_pct"
 * navigates root → "vdut" object → "slope_mv_per_pct" leaf.
 *
 * Mutations stage in the in-RAM tree.  Call tc_config_save() to persist.
 */

#include "tc_config.h"
#include "../storage/storage.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_console.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

static const char *TAG   = "tc_cfg";
static const char *KEY   = "device";

/* In-RAM config tree.  NULL until tc_config_load() is called. */
static cJSON *s_cfg = NULL;

/* ── Factory defaults ────────────────────────────────────────────────────── */

static const char s_defaults[] =
"{"
  "\"fixture_id\":\"UNSET\","
  "\"hw_revision\":\"B\","
  "\"vdut\":{"
    "\"slope_mv_per_pct\":0,"
    "\"intercept_mv\":0,"
    "\"v_nominal_mv\":3300,"
    "\"v_tolerance_pct\":5,"
    "\"settle_ms\":300"
  "},"
  "\"limits\":{"
    "\"i_idle_min_ma\":5,"
    "\"i_idle_max_ma\":250,"
    "\"i_active_min_ma\":50,"
    "\"i_active_max_ma\":400"
  "}"
"}";

/* Note: vdut.slope_mv_per_pct and vdut.intercept_mv are intentionally 0 in
 * the factory defaults.  tc_config_vdut_duty_for_mv() treats slope==0 as
 * "not calibrated" and returns -1 so the caller can refuse to enable VDUT
 * before calibration has been performed and saved. */

/* ── Path traversal ──────────────────────────────────────────────────────── */

/* Walk a dot-separated path into tree.  Returns the leaf node or NULL. */
static cJSON *path_get(cJSON *root, const char *path)
{
    if (!root || !path) return NULL;

    char buf[64];
    strlcpy(buf, path, sizeof(buf));

    cJSON *node = root;
    char  *seg  = buf;
    char  *dot;

    while ((dot = strchr(seg, '.')) != NULL) {
        *dot = '\0';
        node = cJSON_GetObjectItem(node, seg);
        if (!node) return NULL;
        seg = dot + 1;
    }
    return cJSON_GetObjectItem(node, seg);
}

/* Walk to the parent of the leaf, creating intermediate objects if needed.
 * Writes the leaf key into *leaf_key_out.  Returns parent node or NULL. */
static cJSON *path_ensure_parent(cJSON *root, const char *path,
                                  const char **leaf_key_out)
{
    if (!root || !path) return NULL;

    static char buf[64];   /* one call at a time — console is single-threaded */
    strlcpy(buf, path, sizeof(buf));

    cJSON *node = root;
    char  *seg  = buf;
    char  *dot;

    while ((dot = strchr(seg, '.')) != NULL) {
        *dot = '\0';
        cJSON *child = cJSON_GetObjectItem(node, seg);
        if (!child) {
            child = cJSON_AddObjectToObject(node, seg);
            if (!child) return NULL;
        }
        node = child;
        seg  = dot + 1;
    }
    *leaf_key_out = seg;
    return node;
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

void tc_config_reset_defaults(void)
{
    if (s_cfg) { cJSON_Delete(s_cfg); s_cfg = NULL; }
    s_cfg = cJSON_Parse(s_defaults);
    if (!s_cfg)
        ESP_LOGE(TAG, "Failed to parse factory defaults — BUG");
}

int tc_config_load(void)
{
    if (s_cfg) { cJSON_Delete(s_cfg); s_cfg = NULL; }

    int32_t sz = Storage_GetSize(STORAGE_DOMAIN_CONFIGURATION, KEY);
    if (sz <= 0) {
        ESP_LOGI(TAG, "No stored config — using factory defaults");
        tc_config_reset_defaults();
        return 0;
    }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { tc_config_reset_defaults(); return -1; }

    int32_t n = Storage_Read(STORAGE_DOMAIN_CONFIGURATION, KEY,
                              (uint8_t *)buf, (size_t)sz);
    if (n <= 0) {
        free(buf);
        ESP_LOGW(TAG, "Storage read failed — using factory defaults");
        tc_config_reset_defaults();
        return -1;
    }
    buf[n] = '\0';

    s_cfg = cJSON_Parse(buf);
    free(buf);

    if (!s_cfg) {
        ESP_LOGW(TAG, "Stored config is invalid JSON — using factory defaults");
        tc_config_reset_defaults();
        return -1;
    }

    ESP_LOGI(TAG, "Config loaded (%ld bytes)", (long)n);
    return 0;
}

int tc_config_save(void)
{
    if (!s_cfg) {
        ESP_LOGW(TAG, "Nothing to save — config not loaded");
        return -1;
    }
    char *json = cJSON_PrintUnformatted(s_cfg);
    if (!json) return -1;

    int rc = Storage_Write(STORAGE_DOMAIN_CONFIGURATION, KEY,
                           (const uint8_t *)json, strlen(json), false);
    free(json);
    if (rc == 0)
        ESP_LOGI(TAG, "Config saved");
    else
        ESP_LOGW(TAG, "Config save failed");
    return rc;
}

/* ── Read accessors ──────────────────────────────────────────────────────── */

int tc_config_get_int(const char *path, int def)
{
    if (!s_cfg) return def;
    const cJSON *item = path_get(s_cfg, path);
    return (item && cJSON_IsNumber(item)) ? (int)item->valuedouble : def;
}

float tc_config_get_float(const char *path, float def)
{
    if (!s_cfg) return def;
    const cJSON *item = path_get(s_cfg, path);
    return (item && cJSON_IsNumber(item)) ? (float)item->valuedouble : def;
}

const char *tc_config_get_str(const char *path, const char *def)
{
    if (!s_cfg) return def;
    const cJSON *item = path_get(s_cfg, path);
    return (item && cJSON_IsString(item) && item->valuestring)
           ? item->valuestring : def;
}

/* ── Write accessors ─────────────────────────────────────────────────────── */

void tc_config_set_int(const char *path, int val)
{
    if (!s_cfg) tc_config_reset_defaults();
    const char *key;
    cJSON *parent = path_ensure_parent(s_cfg, path, &key);
    if (!parent) return;
    cJSON_DeleteItemFromObject(parent, key);
    cJSON_AddNumberToObject(parent, key, val);
}

void tc_config_set_float(const char *path, float val)
{
    if (!s_cfg) tc_config_reset_defaults();
    const char *key;
    cJSON *parent = path_ensure_parent(s_cfg, path, &key);
    if (!parent) return;
    cJSON_DeleteItemFromObject(parent, key);
    cJSON_AddNumberToObject(parent, key, (double)val);
}

void tc_config_set_str(const char *path, const char *val)
{
    if (!s_cfg) tc_config_reset_defaults();
    const char *key;
    cJSON *parent = path_ensure_parent(s_cfg, path, &key);
    if (!parent) return;
    cJSON_DeleteItemFromObject(parent, key);
    cJSON_AddStringToObject(parent, key, val);
}

/* ── VDUT helpers ────────────────────────────────────────────────────────── */

int tc_config_vdut_duty_for_mv(int target_mv)
{
    int slope     = tc_config_get_int("vdut.slope_mv_per_pct",  0);
    int intercept = tc_config_get_int("vdut.intercept_mv",       0);

    if (slope == 0) {
        ESP_LOGW(TAG, "vdut.slope_mv_per_pct is 0 — VDUT not calibrated");
        return -1;  /* caller must refuse to enable VDUT */
    }

    int duty = (target_mv - intercept) / slope;
    if (duty < 1)  duty = 1;
    if (duty > 99) duty = 99;
    return duty;
}

/* ── Console commands ────────────────────────────────────────────────────── */

static int do_config(int argc, char **argv)
{
    if (argc < 2) goto usage;

    /* config show */
    if (strcmp(argv[1], "show") == 0) {
        if (!s_cfg) { printf("Config not loaded. Run: config load\n"); return 1; }
        char *json = cJSON_Print(s_cfg);
        if (json) { printf("%s\n", json); free(json); }
        return 0;
    }

    /* config load */
    if (strcmp(argv[1], "load") == 0) {
        int rc = tc_config_load();
        printf("Config %s\n", rc == 0 ? "loaded" : "load FAILED — using defaults");
        return rc;
    }

    /* config save */
    if (strcmp(argv[1], "save") == 0) {
        int rc = tc_config_save();
        printf("Config %s\n", rc == 0 ? "saved" : "save FAILED");
        return rc;
    }

    /* config reset */
    if (strcmp(argv[1], "reset") == 0) {
        tc_config_reset_defaults();
        printf("Config reset to factory defaults (not saved — run: config save)\n");
        return 0;
    }

    /* config get <key.path> */
    if (strcmp(argv[1], "get") == 0) {
        if (argc < 3) { printf("Usage: config get <key.path>\n"); return 1; }
        if (!s_cfg) { printf("Config not loaded\n"); return 1; }
        const cJSON *item = path_get(s_cfg, argv[2]);
        if (!item) {
            printf("%s: (not found)\n", argv[2]);
            return 1;
        }
        char *val = cJSON_PrintUnformatted(item);
        if (val) { printf("%s = %s\n", argv[2], val); free(val); }
        return 0;
    }

    /* config set <key.path> <value>
     * Value type: integer if it parses cleanly as long, float if it has a '.',
     * otherwise string. */
    if (strcmp(argv[1], "set") == 0) {
        if (argc < 4) { printf("Usage: config set <key.path> <value>\n"); return 1; }
        const char *path = argv[2];
        const char *val  = argv[3];

        /* Detect type */
        char *endptr = NULL;
        errno = 0;
        long lval = strtol(val, &endptr, 10);
        if (errno == 0 && endptr != val && *endptr == '\0') {
            tc_config_set_int(path, (int)lval);
            printf("%s = %ld  (int, not saved — run: config save)\n", path, lval);
        } else {
            errno = 0;
            double dval = strtod(val, &endptr);
            if (errno == 0 && endptr != val && *endptr == '\0') {
                tc_config_set_float(path, (float)dval);
                printf("%s = %g  (float, not saved — run: config save)\n", path, dval);
            } else {
                tc_config_set_str(path, val);
                printf("%s = \"%s\"  (string, not saved — run: config save)\n", path, val);
            }
        }
        return 0;
    }

    /* config vdut-duty <target_mv>  — diagnostic: compute duty% for target */
    if (strcmp(argv[1], "vdut-duty") == 0) {
        if (argc < 3) { printf("Usage: config vdut-duty <target_mv>\n"); return 1; }
        int target_mv = atoi(argv[2]);
        int duty = tc_config_vdut_duty_for_mv(target_mv);
        if (duty < 0)
            printf("VDUT not calibrated — set vdut.slope_mv_per_pct and vdut.intercept_mv first\n");
        else
            printf("Target %d mV → duty %d%%  "
                   "(slope=%d  intercept=%d)\n",
                   target_mv, duty,
                   tc_config_get_int("vdut.slope_mv_per_pct", 0),
                   tc_config_get_int("vdut.intercept_mv", 0));
        return 0;
    }

usage:
    printf("Usage:\n"
           "  config show                   print current config as JSON\n"
           "  config get <key.path>         read one value\n"
           "  config set <key.path> <val>   set one value (auto-typed)\n"
           "  config load                   reload from storage\n"
           "  config save                   write to storage\n"
           "  config reset                  restore factory defaults (in RAM)\n"
           "  config vdut-duty <mv>         compute duty%% for target voltage\n"
           "\n"
           "VDUT calibration:\n"
           "  config set vdut.slope_mv_per_pct <slope>\n"
           "  config set vdut.intercept_mv <intercept>\n"
           "  config save\n"
           "  config vdut-duty 3300         verify: should print ~90%%\n");
    return 1;
}

void register_config_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "config",
        .help    = "Device config: config <show|get|set|load|save|reset|vdut-duty>",
        .hint    = NULL,
        .func    = &do_config,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
