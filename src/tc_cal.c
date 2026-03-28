/* tc_cal.c — Calibration storage and application for all measurement channels.
 *
 * Storage: STORAGE_DOMAIN_CALIBRATION, key "cal", JSON blob.
 *
 * All channels use a linear correction model:
 *   corrected = gain × raw + offset
 *
 * VDUT uses the inverting regulator model:
 *   V_mv = slope_mv_per_pct × duty_pct + intercept_mv
 *
 * Factory defaults: gain=1.0, offset=0 (passthrough) for all channels.
 * VDUT slope=0 is the uncalibrated sentinel.
 */

#include "tc_cal.h"
#include "../storage/storage.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_console.h"
#ifndef NATIVE_BUILD
#include "version.h"
#else
#define FW_VERSION_STRING "unknown"
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

static const char *TAG = "tc_cal";
static const char *KEY = "cal";

/* ── In-RAM calibration data ─────────────────────────────────────────────── */

typedef struct {
    /* VDUT PWM DAC — inverting model; ch0=VDUT1, ch1=VDUT2; 0=uncalibrated */
    int   vdut_slope_mv_per_pct[TC_CAL_VDUT_NCH];
    int   vdut_intercept_mv[TC_CAL_VDUT_NCH];

    /* INA219 voltage (Vbus) per channel */
    float ina_v_gain[TC_CAL_INA_NCH];
    int   ina_v_offset_mv[TC_CAL_INA_NCH];

    /* INA219 current per channel */
    float ina_i_gain[TC_CAL_INA_NCH];
    int   ina_i_offset_ma[TC_CAL_INA_NCH];

    /* ADC128D818 per channel */
    float adc_gain[TC_CAL_ADC_NCH];
    int   adc_offset_mv[TC_CAL_ADC_NCH];

    /* ISO-8601 UTC timestamp of last save; empty if never saved with valid clock */
    char cal_timestamp[32];
} cal_data_t;

static cal_data_t s_cal;
static bool       s_loaded = false;

/* ── Factory defaults ────────────────────────────────────────────────────── */

static void apply_defaults(cal_data_t *c)
{
    for (int i = 0; i < TC_CAL_VDUT_NCH; i++) {
        c->vdut_slope_mv_per_pct[i] = 0;   /* uncalibrated sentinel */
        c->vdut_intercept_mv[i]     = 0;
    }

    for (int i = 0; i < TC_CAL_INA_NCH; i++) {
        c->ina_v_gain[i]      = 1.0f;
        c->ina_v_offset_mv[i] = 0;
        c->ina_i_gain[i]      = 1.0f;
        c->ina_i_offset_ma[i] = 0;
    }
    for (int i = 0; i < TC_CAL_ADC_NCH; i++) {
        c->adc_gain[i]      = 1.0f;
        c->adc_offset_mv[i] = 0;
    }
    c->cal_timestamp[0] = '\0';
}

/* ── JSON serialization ──────────────────────────────────────────────────── */

static cJSON *cal_to_json(const cal_data_t *c)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    /* VDUT — two channels */
    cJSON *vdut = cJSON_AddObjectToObject(root, "vdut");
    for (int ch = 0; ch < TC_CAL_VDUT_NCH; ch++) {
        char vkey[8];
        snprintf(vkey, sizeof(vkey), "ch%d", ch);
        cJSON *vc = cJSON_AddObjectToObject(vdut, vkey);
        cJSON_AddNumberToObject(vc, "slope_mv_per_pct", c->vdut_slope_mv_per_pct[ch]);
        cJSON_AddNumberToObject(vc, "intercept_mv",     c->vdut_intercept_mv[ch]);
    }

    /* INA219 */
    cJSON *ina = cJSON_AddObjectToObject(root, "ina219");
    for (int ch = 0; ch < TC_CAL_INA_NCH; ch++) {
        char key[8];
        snprintf(key, sizeof(key), "ch%d", ch);
        cJSON *c_node = cJSON_AddObjectToObject(ina, key);
        cJSON_AddNumberToObject(c_node, "v_gain",      (double)c->ina_v_gain[ch]);
        cJSON_AddNumberToObject(c_node, "v_offset_mv", c->ina_v_offset_mv[ch]);
        cJSON_AddNumberToObject(c_node, "i_gain",      (double)c->ina_i_gain[ch]);
        cJSON_AddNumberToObject(c_node, "i_offset_ma", c->ina_i_offset_ma[ch]);
    }

    /* ADC128 */
    cJSON *adc = cJSON_AddObjectToObject(root, "adc128");
    for (int ch = 0; ch < TC_CAL_ADC_NCH; ch++) {
        char key[8];
        snprintf(key, sizeof(key), "ch%d", ch);
        cJSON *c_node = cJSON_AddObjectToObject(adc, key);
        cJSON_AddNumberToObject(c_node, "gain",      (double)c->adc_gain[ch]);
        cJSON_AddNumberToObject(c_node, "offset_mv", c->adc_offset_mv[ch]);
    }

    /* Timestamp */
    cJSON_AddStringToObject(root, "cal_timestamp", c->cal_timestamp);

    return root;
}

static void json_to_cal(const cJSON *root, cal_data_t *c)
{
    apply_defaults(c);   /* start from defaults — missing keys stay at default */

    /* VDUT — new format: ch0/ch1 sub-objects; old format: slope/intercept directly */
    const cJSON *vdut = cJSON_GetObjectItem(root, "vdut");
    if (vdut) {
        const cJSON *ch0_node = cJSON_GetObjectItem(vdut, "ch0");
        if (ch0_node) {
            /* New per-channel format */
            for (int ch = 0; ch < TC_CAL_VDUT_NCH; ch++) {
                char vkey[8];
                snprintf(vkey, sizeof(vkey), "ch%d", ch);
                const cJSON *vc = cJSON_GetObjectItem(vdut, vkey);
                if (!vc) continue;
                const cJSON *s = cJSON_GetObjectItem(vc, "slope_mv_per_pct");
                const cJSON *i = cJSON_GetObjectItem(vc, "intercept_mv");
                if (s && cJSON_IsNumber(s)) c->vdut_slope_mv_per_pct[ch] = (int)s->valuedouble;
                if (i && cJSON_IsNumber(i)) c->vdut_intercept_mv[ch]     = (int)i->valuedouble;
            }
        } else {
            /* Old single-channel format — migrate to ch0; ch1 stays at default */
            const cJSON *s = cJSON_GetObjectItem(vdut, "slope_mv_per_pct");
            const cJSON *i = cJSON_GetObjectItem(vdut, "intercept_mv");
            if (s && cJSON_IsNumber(s)) c->vdut_slope_mv_per_pct[0] = (int)s->valuedouble;
            if (i && cJSON_IsNumber(i)) c->vdut_intercept_mv[0]     = (int)i->valuedouble;
        }
    }

    /* INA219 */
    const cJSON *ina = cJSON_GetObjectItem(root, "ina219");
    if (ina) {
        for (int ch = 0; ch < TC_CAL_INA_NCH; ch++) {
            char key[8];
            snprintf(key, sizeof(key), "ch%d", ch);
            const cJSON *c_node = cJSON_GetObjectItem(ina, key);
            if (!c_node) continue;
            const cJSON *f;
            f = cJSON_GetObjectItem(c_node, "v_gain");
            if (f && cJSON_IsNumber(f)) c->ina_v_gain[ch] = (float)f->valuedouble;
            f = cJSON_GetObjectItem(c_node, "v_offset_mv");
            if (f && cJSON_IsNumber(f)) c->ina_v_offset_mv[ch] = (int)f->valuedouble;
            f = cJSON_GetObjectItem(c_node, "i_gain");
            if (f && cJSON_IsNumber(f)) c->ina_i_gain[ch] = (float)f->valuedouble;
            f = cJSON_GetObjectItem(c_node, "i_offset_ma");
            if (f && cJSON_IsNumber(f)) c->ina_i_offset_ma[ch] = (int)f->valuedouble;
        }
    }

    /* ADC128 */
    const cJSON *adc = cJSON_GetObjectItem(root, "adc128");
    if (adc) {
        for (int ch = 0; ch < TC_CAL_ADC_NCH; ch++) {
            char key[8];
            snprintf(key, sizeof(key), "ch%d", ch);
            const cJSON *c_node = cJSON_GetObjectItem(adc, key);
            if (!c_node) continue;
            const cJSON *f;
            f = cJSON_GetObjectItem(c_node, "gain");
            if (f && cJSON_IsNumber(f)) c->adc_gain[ch] = (float)f->valuedouble;
            f = cJSON_GetObjectItem(c_node, "offset_mv");
            if (f && cJSON_IsNumber(f)) c->adc_offset_mv[ch] = (int)f->valuedouble;
        }
    }

    /* Timestamp */
    const cJSON *ts = cJSON_GetObjectItem(root, "cal_timestamp");
    if (ts && cJSON_IsString(ts) && ts->valuestring)
        snprintf(c->cal_timestamp, sizeof(c->cal_timestamp), "%s", ts->valuestring);
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

void tc_cal_reset(void)
{
    apply_defaults(&s_cal);
    s_loaded = true;
    ESP_LOGI(TAG, "Calibration reset to factory defaults");
}

int tc_cal_load(void)
{
    int32_t sz = Storage_GetSize(STORAGE_DOMAIN_CALIBRATION, KEY);
    if (sz <= 0) {
        ESP_LOGI(TAG, "No stored calibration — using factory defaults");
        tc_cal_reset();
        return 0;
    }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        ESP_LOGE(TAG, "OOM — using factory defaults");
        tc_cal_reset();
        return -1;
    }

    int32_t n = Storage_Read(STORAGE_DOMAIN_CALIBRATION, KEY,
                              (uint8_t *)buf, (size_t)sz);
    if (n <= 0) {
        free(buf);
        ESP_LOGW(TAG, "Storage read failed — using factory defaults");
        tc_cal_reset();
        return -1;
    }
    buf[n] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);

    if (!root) {
        ESP_LOGW(TAG, "Stored calibration is invalid JSON — using factory defaults");
        tc_cal_reset();
        return -1;
    }

    json_to_cal(root, &s_cal);
    cJSON_Delete(root);
    s_loaded = true;

    ESP_LOGI(TAG, "Calibration loaded (%ld bytes)", (long)n);
    tc_cal_write_profile_json();
    return 0;
}

int tc_cal_save(void)
{
    if (!s_loaded) {
        ESP_LOGW(TAG, "Calibration not loaded — cannot save");
        return -1;
    }

    /* Stamp with current UTC time if clock is synchronised (year >= 2020) */
    time_t now = time(NULL);
    if (now > 1577836800LL) {   /* 2020-01-01 00:00:00 UTC */
        struct tm tm_utc;
        gmtime_r(&now, &tm_utc);
        strftime(s_cal.cal_timestamp, sizeof(s_cal.cal_timestamp),
                 "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    }

    cJSON *root = cal_to_json(&s_cal);
    if (!root) return -1;

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return -1;

    int rc = Storage_Write(STORAGE_DOMAIN_CALIBRATION, KEY,
                           (const uint8_t *)json, strlen(json), false);
    free(json);

    if (rc == 0) {
        ESP_LOGI(TAG, "Calibration saved");
        tc_cal_write_profile_json();
    } else {
        ESP_LOGW(TAG, "Calibration save failed");
    }
    return rc;
}

/* ── VDUT ────────────────────────────────────────────────────────────────── */

void tc_cal_get_vdut(int ch, int *slope_out, int *intercept_out)
{
    if (ch < 0 || ch >= TC_CAL_VDUT_NCH) return;
    if (slope_out)     *slope_out     = s_cal.vdut_slope_mv_per_pct[ch];
    if (intercept_out) *intercept_out = s_cal.vdut_intercept_mv[ch];
}

void tc_cal_set_vdut(int ch, int slope_mv_per_pct, int intercept_mv)
{
    if (ch < 0 || ch >= TC_CAL_VDUT_NCH) return;
    s_cal.vdut_slope_mv_per_pct[ch] = slope_mv_per_pct;
    s_cal.vdut_intercept_mv[ch]     = intercept_mv;
}

int tc_cal_vdut_duty_for_mv(int ch, int target_mv)
{
    if (ch < 0 || ch >= TC_CAL_VDUT_NCH) return -1;
    if (s_cal.vdut_slope_mv_per_pct[ch] == 0) {
        ESP_LOGW(TAG, "VDUT%d not calibrated (slope=0)", ch + 1);
        return -1;
    }
    int duty = (target_mv - s_cal.vdut_intercept_mv[ch]) / s_cal.vdut_slope_mv_per_pct[ch];
    if (duty < 1)  duty = 1;
    if (duty > 99) duty = 99;
    return duty;
}

/* ── INA219 ──────────────────────────────────────────────────────────────── */

void tc_cal_get_ina_v(int ch, float *gain_out, int *offset_mv_out)
{
    if (ch < 0 || ch >= TC_CAL_INA_NCH) return;
    if (gain_out)      *gain_out      = s_cal.ina_v_gain[ch];
    if (offset_mv_out) *offset_mv_out = s_cal.ina_v_offset_mv[ch];
}

void tc_cal_set_ina_v(int ch, float gain, int offset_mv)
{
    if (ch < 0 || ch >= TC_CAL_INA_NCH) return;
    s_cal.ina_v_gain[ch]      = gain;
    s_cal.ina_v_offset_mv[ch] = offset_mv;
}

void tc_cal_get_ina_i(int ch, float *gain_out, int *offset_ma_out)
{
    if (ch < 0 || ch >= TC_CAL_INA_NCH) return;
    if (gain_out)      *gain_out      = s_cal.ina_i_gain[ch];
    if (offset_ma_out) *offset_ma_out = s_cal.ina_i_offset_ma[ch];
}

void tc_cal_set_ina_i(int ch, float gain, int offset_ma)
{
    if (ch < 0 || ch >= TC_CAL_INA_NCH) return;
    s_cal.ina_i_gain[ch]      = gain;
    s_cal.ina_i_offset_ma[ch] = offset_ma;
}

int tc_cal_apply_ina_v(int ch, int raw_mv)
{
    if (ch < 0 || ch >= TC_CAL_INA_NCH) return raw_mv;
    return (int)(s_cal.ina_v_gain[ch] * (float)raw_mv) + s_cal.ina_v_offset_mv[ch];
}

int tc_cal_apply_ina_i(int ch, int raw_ma)
{
    if (ch < 0 || ch >= TC_CAL_INA_NCH) return raw_ma;
    return (int)(s_cal.ina_i_gain[ch] * (float)raw_ma) + s_cal.ina_i_offset_ma[ch];
}

/* ── ADC128D818 ──────────────────────────────────────────────────────────── */

void tc_cal_get_adc(int ch, float *gain_out, int *offset_mv_out)
{
    if (ch < 0 || ch >= TC_CAL_ADC_NCH) return;
    if (gain_out)      *gain_out      = s_cal.adc_gain[ch];
    if (offset_mv_out) *offset_mv_out = s_cal.adc_offset_mv[ch];
}

void tc_cal_set_adc(int ch, float gain, int offset_mv)
{
    if (ch < 0 || ch >= TC_CAL_ADC_NCH) return;
    s_cal.adc_gain[ch]      = gain;
    s_cal.adc_offset_mv[ch] = offset_mv;
}

int tc_cal_apply_adc(int ch, int raw_mv)
{
    if (ch < 0 || ch >= TC_CAL_ADC_NCH) return raw_mv;
    return (int)(s_cal.adc_gain[ch] * (float)raw_mv) + s_cal.adc_offset_mv[ch];
}

/* ── Timestamp & expiry ──────────────────────────────────────────────────── */

void tc_cal_get_timestamp(char *buf, size_t len)
{
    snprintf(buf, len, "%s", s_cal.cal_timestamp);
}

int tc_cal_is_expired(int max_age_days)
{
    if (s_cal.cal_timestamp[0] == '\0')
        return -1;   /* never stamped */

    /* Check if system clock is synchronised (epoch > 2020-01-01) */
    time_t now = time(NULL);
    if (now <= 1577836800LL)
        return -2;   /* clock not synchronised — cannot determine age */

    /* Parse stored ISO-8601: "YYYY-MM-DDTHH:MM:SSZ" */
    int y, mo, d, h, mi, s;
    if (sscanf(s_cal.cal_timestamp, "%d-%d-%dT%d:%d:%dZ",
               &y, &mo, &d, &h, &mi, &s) != 6)
        return -1;   /* parse failed — treat as no timestamp */

    struct tm tm_cal = {
        .tm_year = y - 1900,
        .tm_mon  = mo - 1,
        .tm_mday = d,
        .tm_hour = h,
        .tm_min  = mi,
        .tm_sec  = s,
        .tm_isdst = 0,
    };
    time_t cal_time = timegm(&tm_cal);
    if (cal_time < 0)
        return -1;

    double age_days = difftime(now, cal_time) / 86400.0;
    return (age_days > (double)max_age_days) ? 1 : 0;
}

/* ── LittleFS fixture snapshot ───────────────────────────────────────────── */

#define CAL_PROFILE_PATH  "/recipes/config/cal-profile.json"
#define CAL_PROFILE_DIR   "/recipes/config"

/* Six-month calibration interval per ISO 9001 §7.1.5. */
#define CAL_EXPIRY_DAYS  180

void tc_cal_write_profile_json(void)
{
    if (!s_loaded) return;

    /* Ensure /recipes/config/ directory exists. */
    mkdir(CAL_PROFILE_DIR, 0755);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGW(TAG, "cal_profile: OOM building JSON");
        return;
    }

    cJSON_AddStringToObject(root, "fixture_type", "G3-MB-Single-Channel");
    cJSON_AddStringToObject(root, "firmware_version", FW_VERSION_STRING);

    /* calibration_date / calibration_expiry from stored timestamp. */
    char cal_date[16] = "unknown";
    char cal_expiry[16] = "unknown";
    if (s_cal.cal_timestamp[0] != '\0') {
        /* Extract YYYY-MM-DD from "YYYY-MM-DDTHH:MM:SSZ" */
        snprintf(cal_date, sizeof(cal_date), "%.10s", s_cal.cal_timestamp);

        /* Compute expiry = cal_date + CAL_EXPIRY_DAYS */
        int y, mo, d, h, mi, s;
        if (sscanf(s_cal.cal_timestamp, "%d-%d-%dT%d:%d:%dZ",
                   &y, &mo, &d, &h, &mi, &s) == 6) {
            struct tm tm_cal = {
                .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d,
                .tm_hour = 0, .tm_min = 0, .tm_sec = 0, .tm_isdst = 0,
            };
            time_t cal_t = mktime(&tm_cal);
            if (cal_t >= 0) {
                time_t exp_t = cal_t + (time_t)CAL_EXPIRY_DAYS * 86400;
                struct tm tm_exp;
                gmtime_r(&exp_t, &tm_exp);
                strftime(cal_expiry, sizeof(cal_expiry), "%Y-%m-%d", &tm_exp);
            }
        }
    }
    cJSON_AddStringToObject(root, "calibration_date",   cal_date);
    cJSON_AddStringToObject(root, "calibration_expiry", cal_expiry);

    /* instruments[] — one entry per measurement instrument. */
    cJSON *instruments = cJSON_AddArrayToObject(root, "instruments");

    /* INA219 ch0 and ch1 */
    for (int ch = 0; ch < TC_CAL_INA_NCH; ch++) {
        cJSON *inst = cJSON_CreateObject();
        char id[20];
        snprintf(id, sizeof(id), "ina219_ch%d", ch);
        cJSON_AddStringToObject(inst, "id", id);
        cJSON_AddStringToObject(inst, "type", "INA219");
        char role[48];
        snprintf(role, sizeof(role), "DUT output current (VDUT%d)", ch + 1);
        cJSON_AddStringToObject(inst, "role", role);
        cJSON_AddNumberToObject(inst, "accuracy_pct", 0.5);
        cJSON_AddStringToObject(inst, "accuracy_source", "datasheet");
        /* INA219 calibration register — derived from current LSB (10µA → 0x2000) */
        cJSON_AddStringToObject(inst, "cal_register", "0x2000");
        cJSON_AddItemToArray(instruments, inst);
    }

    /* ADC128D818 */
    {
        cJSON *inst = cJSON_CreateObject();
        cJSON_AddStringToObject(inst, "id", "adc128d818");
        cJSON_AddStringToObject(inst, "type", "ADC128D818");
        cJSON_AddStringToObject(inst, "role", "Rail voltages + continuity scan");
        cJSON_AddNumberToObject(inst, "accuracy_pct", 0.45);
        cJSON_AddStringToObject(inst, "accuracy_source", "datasheet");
        cJSON_AddNumberToObject(inst, "vref_v", 3.3);
        cJSON_AddItemToArray(instruments, inst);
    }

    /* PWM+RC DAC (VDUT) ch0 and ch1 */
    for (int ch = 0; ch < TC_CAL_VDUT_NCH; ch++) {
        cJSON *inst = cJSON_CreateObject();
        char id[20];
        snprintf(id, sizeof(id), "pwm_dac_ch%d", ch);
        cJSON_AddStringToObject(inst, "id", id);
        cJSON_AddStringToObject(inst, "type", "PWM+RC");
        char role[48];
        snprintf(role, sizeof(role), "VDUT%d adjustable voltage stimulus", ch + 1);
        cJSON_AddStringToObject(inst, "role", role);
        /* slope and intercept from calibration; 0 = uncalibrated */
        cJSON_AddNumberToObject(inst, "slope",     (double)s_cal.vdut_slope_mv_per_pct[ch]);
        cJSON_AddNumberToObject(inst, "intercept", (double)s_cal.vdut_intercept_mv[ch]);
        cJSON_AddStringToObject(inst, "accuracy_source",
            s_cal.vdut_slope_mv_per_pct[ch] != 0 ? "characterized_c5b" : "uncalibrated");
        cJSON_AddItemToArray(instruments, inst);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        ESP_LOGW(TAG, "cal_profile: cJSON print failed");
        return;
    }

    FILE *f = fopen(CAL_PROFILE_PATH, "w");
    if (!f) {
        ESP_LOGW(TAG, "cal_profile: cannot open %s for write (errno=%d)", CAL_PROFILE_PATH, errno);
        free(json);
        return;
    }
    fputs(json, f);
    fclose(f);
    free(json);

    ESP_LOGI(TAG, "cal_profile written: %s", CAL_PROFILE_PATH);
}

/* ── Console commands ────────────────────────────────────────────────────── */
/*
 * cal show                        — dump all calibration as JSON
 * cal load                        — reload from storage
 * cal save                        — persist to storage
 * cal reset                       — restore factory defaults (in RAM)
 *
 * cal vdut <slope> <intercept>    — set VDUT PWM slope + intercept
 * cal vdut-duty <target_mv>       — diagnostic: compute duty% for target
 *
 * cal ina <ch> v <gain> <offset_mv>  — INA219 voltage calibration
 * cal ina <ch> i <gain> <offset_ma>  — INA219 current calibration
 *
 * cal adc <ch> <gain> <offset_mv>    — ADC128 per-channel calibration
 */

static int do_cal(int argc, char **argv)
{
    if (argc < 2) goto usage;

    /* cal show */
    if (strcmp(argv[1], "show") == 0) {
        if (!s_loaded) { printf("Calibration not loaded. Run: cal load\n"); return 1; }
        cJSON *root = cal_to_json(&s_cal);
        if (!root) { printf("OOM\n"); return 1; }
        char *js = cJSON_Print(root);
        cJSON_Delete(root);
        if (js) { printf("%s\n", js); free(js); }
        return 0;
    }

    /* cal load */
    if (strcmp(argv[1], "load") == 0) {
        int rc = tc_cal_load();
        printf("Calibration %s\n", rc == 0 ? "loaded" : "load FAILED — using defaults");
        return rc;
    }

    /* cal save */
    if (strcmp(argv[1], "save") == 0) {
        int rc = tc_cal_save();
        printf("Calibration %s\n", rc == 0 ? "saved" : "save FAILED");
        return rc;
    }

    /* cal reset */
    if (strcmp(argv[1], "reset") == 0) {
        tc_cal_reset();
        printf("Calibration reset to factory defaults (not saved — run: cal save)\n");
        return 0;
    }

    /* cal vdut <ch> <slope> <intercept> */
    if (strcmp(argv[1], "vdut") == 0) {
        if (argc < 5) {
            printf("Usage: cal vdut <ch 0|1> <slope_mv_per_pct> <intercept_mv>\n");
            return 1;
        }
        int ch        = atoi(argv[2]);
        int slope     = atoi(argv[3]);
        int intercept = atoi(argv[4]);
        if (ch < 0 || ch >= TC_CAL_VDUT_NCH) {
            printf("VDUT channel must be 0 (VDUT1) or 1 (VDUT2)\n"); return 1;
        }
        tc_cal_set_vdut(ch, slope, intercept);
        printf("VDUT%d cal: slope=%d mV/%%  intercept=%d mV  "
               "(not saved — run: cal save)\n", ch + 1, slope, intercept);
        return 0;
    }

    /* cal vdut-duty <ch> <target_mv> */
    if (strcmp(argv[1], "vdut-duty") == 0) {
        if (argc < 4) { printf("Usage: cal vdut-duty <ch 0|1> <target_mv>\n"); return 1; }
        int ch        = atoi(argv[2]);
        int target_mv = atoi(argv[3]);
        if (ch < 0 || ch >= TC_CAL_VDUT_NCH) {
            printf("VDUT channel must be 0 (VDUT1) or 1 (VDUT2)\n"); return 1;
        }
        int duty = tc_cal_vdut_duty_for_mv(ch, target_mv);
        if (duty < 0)
            printf("VDUT%d not calibrated — run: cal vdut %d <slope> <intercept> && cal save\n",
                   ch + 1, ch);
        else
            printf("VDUT%d: target %d mV → duty %d%%  (slope=%d  intercept=%d)\n",
                   ch + 1, target_mv, duty,
                   s_cal.vdut_slope_mv_per_pct[ch], s_cal.vdut_intercept_mv[ch]);
        return 0;
    }

    /* cal ina <ch> v|i <gain> <offset> */
    if (strcmp(argv[1], "ina") == 0) {
        if (argc < 6) {
            printf("Usage: cal ina <ch> <v|i> <gain> <offset_mv|offset_ma>\n");
            return 1;
        }
        int ch = atoi(argv[2]);
        if (ch < 0 || ch >= TC_CAL_INA_NCH) {
            printf("INA219 channel must be 0 or 1\n");
            return 1;
        }
        char  *endptr;
        float  gain   = (float)strtod(argv[4], &endptr);
        int    offset = atoi(argv[5]);

        if (strcmp(argv[3], "v") == 0) {
            tc_cal_set_ina_v(ch, gain, offset);
            printf("INA219 ch%d voltage cal: gain=%.4f  offset=%d mV  "
                   "(not saved — run: cal save)\n", ch, (double)gain, offset);
        } else if (strcmp(argv[3], "i") == 0) {
            tc_cal_set_ina_i(ch, gain, offset);
            printf("INA219 ch%d current cal: gain=%.4f  offset=%d mA  "
                   "(not saved — run: cal save)\n", ch, (double)gain, offset);
        } else {
            printf("Usage: cal ina <ch> <v|i> <gain> <offset>\n");
            return 1;
        }
        return 0;
    }

    /* cal adc <ch> <gain> <offset_mv> */
    if (strcmp(argv[1], "adc") == 0) {
        if (argc < 5) {
            printf("Usage: cal adc <ch> <gain> <offset_mv>\n");
            return 1;
        }
        int   ch     = atoi(argv[2]);
        float gain   = (float)strtod(argv[3], NULL);
        int   offset = atoi(argv[4]);
        if (ch < 0 || ch >= TC_CAL_ADC_NCH) {
            printf("ADC128 channel must be 0–7\n");
            return 1;
        }
        tc_cal_set_adc(ch, gain, offset);
        printf("ADC128 ch%d cal: gain=%.4f  offset=%d mV  "
               "(not saved — run: cal save)\n", ch, (double)gain, offset);
        return 0;
    }

usage:
    printf("Usage:\n"
           "  cal show                              print all calibration as JSON\n"
           "  cal load                              reload from storage\n"
           "  cal save                              write to storage\n"
           "  cal reset                             restore factory defaults (in RAM)\n"
           "\n"
           "  cal vdut <ch> <slope> <intercept>     VDUT PWM slope_mv_per_pct + intercept_mv\n"
           "                                          ch=0 → VDUT1,  ch=1 → VDUT2\n"
           "  cal vdut-duty <ch> <target_mv>        compute duty%% for target voltage\n"
           "\n"
           "  cal ina <ch> v <gain> <offset_mv>     INA219 ch voltage correction\n"
           "  cal ina <ch> i <gain> <offset_ma>     INA219 ch current correction\n"
           "\n"
           "  cal adc <ch> <gain> <offset_mv>       ADC128D818 channel correction\n"
           "\n"
           "Example — set VDUT calibration:\n"
           "  cal vdut 0 -87 11200\n"
           "  cal vdut 1 -89 11400\n"
           "  cal save\n"
           "  cal vdut-duty 0 3300   → prints duty for VDUT1\n");
    return 1;
}

void register_cal_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "cal",
        .help    = "Calibration: cal <show|load|save|reset|vdut|ina|adc>",
        .hint    = NULL,
        .func    = &do_cal,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
