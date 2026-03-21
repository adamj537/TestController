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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

static const char *TAG = "tc_cal";
static const char *KEY = "cal";

/* ── In-RAM calibration data ─────────────────────────────────────────────── */

typedef struct {
    /* VDUT PWM DAC — inverting model */
    int   vdut_slope_mv_per_pct;   /* 0 = uncalibrated */
    int   vdut_intercept_mv;

    /* INA219 voltage (Vbus) per channel */
    float ina_v_gain[TC_CAL_INA_NCH];
    int   ina_v_offset_mv[TC_CAL_INA_NCH];

    /* INA219 current per channel */
    float ina_i_gain[TC_CAL_INA_NCH];
    int   ina_i_offset_ma[TC_CAL_INA_NCH];

    /* ADC128D818 per channel */
    float adc_gain[TC_CAL_ADC_NCH];
    int   adc_offset_mv[TC_CAL_ADC_NCH];
} cal_data_t;

static cal_data_t s_cal;
static bool       s_loaded = false;

/* ── Factory defaults ────────────────────────────────────────────────────── */

static void apply_defaults(cal_data_t *c)
{
    c->vdut_slope_mv_per_pct = 0;   /* uncalibrated sentinel */
    c->vdut_intercept_mv     = 0;

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
}

/* ── JSON serialization ──────────────────────────────────────────────────── */

static cJSON *cal_to_json(const cal_data_t *c)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    /* VDUT */
    cJSON *vdut = cJSON_AddObjectToObject(root, "vdut");
    cJSON_AddNumberToObject(vdut, "slope_mv_per_pct", c->vdut_slope_mv_per_pct);
    cJSON_AddNumberToObject(vdut, "intercept_mv",     c->vdut_intercept_mv);

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

    return root;
}

static void json_to_cal(const cJSON *root, cal_data_t *c)
{
    apply_defaults(c);   /* start from defaults — missing keys stay at default */

    /* VDUT */
    const cJSON *vdut = cJSON_GetObjectItem(root, "vdut");
    if (vdut) {
        const cJSON *s = cJSON_GetObjectItem(vdut, "slope_mv_per_pct");
        const cJSON *i = cJSON_GetObjectItem(vdut, "intercept_mv");
        if (s && cJSON_IsNumber(s)) c->vdut_slope_mv_per_pct = (int)s->valuedouble;
        if (i && cJSON_IsNumber(i)) c->vdut_intercept_mv     = (int)i->valuedouble;
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
    return 0;
}

int tc_cal_save(void)
{
    if (!s_loaded) {
        ESP_LOGW(TAG, "Calibration not loaded — cannot save");
        return -1;
    }

    cJSON *root = cal_to_json(&s_cal);
    if (!root) return -1;

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return -1;

    int rc = Storage_Write(STORAGE_DOMAIN_CALIBRATION, KEY,
                           (const uint8_t *)json, strlen(json), false);
    free(json);

    if (rc == 0)
        ESP_LOGI(TAG, "Calibration saved");
    else
        ESP_LOGW(TAG, "Calibration save failed");
    return rc;
}

/* ── VDUT ────────────────────────────────────────────────────────────────── */

void tc_cal_get_vdut(int *slope_out, int *intercept_out)
{
    if (slope_out)     *slope_out     = s_cal.vdut_slope_mv_per_pct;
    if (intercept_out) *intercept_out = s_cal.vdut_intercept_mv;
}

void tc_cal_set_vdut(int slope_mv_per_pct, int intercept_mv)
{
    s_cal.vdut_slope_mv_per_pct = slope_mv_per_pct;
    s_cal.vdut_intercept_mv     = intercept_mv;
}

int tc_cal_vdut_duty_for_mv(int target_mv)
{
    if (s_cal.vdut_slope_mv_per_pct == 0) {
        ESP_LOGW(TAG, "VDUT not calibrated (slope=0)");
        return -1;
    }
    int duty = (target_mv - s_cal.vdut_intercept_mv) / s_cal.vdut_slope_mv_per_pct;
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

    /* cal vdut <slope> <intercept> */
    if (strcmp(argv[1], "vdut") == 0) {
        if (argc < 4) {
            printf("Usage: cal vdut <slope_mv_per_pct> <intercept_mv>\n");
            return 1;
        }
        int slope     = atoi(argv[2]);
        int intercept = atoi(argv[3]);
        tc_cal_set_vdut(slope, intercept);
        printf("VDUT cal: slope=%d mV/%%  intercept=%d mV  "
               "(not saved — run: cal save)\n", slope, intercept);
        return 0;
    }

    /* cal vdut-duty <target_mv> */
    if (strcmp(argv[1], "vdut-duty") == 0) {
        if (argc < 3) { printf("Usage: cal vdut-duty <target_mv>\n"); return 1; }
        int target_mv = atoi(argv[2]);
        int duty = tc_cal_vdut_duty_for_mv(target_mv);
        if (duty < 0)
            printf("VDUT not calibrated — run: cal vdut <slope> <intercept> && cal save\n");
        else
            printf("Target %d mV → duty %d%%  (slope=%d  intercept=%d)\n",
                   target_mv, duty,
                   s_cal.vdut_slope_mv_per_pct, s_cal.vdut_intercept_mv);
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
           "  cal show                           print all calibration as JSON\n"
           "  cal load                           reload from storage\n"
           "  cal save                           write to storage\n"
           "  cal reset                          restore factory defaults (in RAM)\n"
           "\n"
           "  cal vdut <slope> <intercept>       VDUT PWM slope_mv_per_pct + intercept_mv\n"
           "  cal vdut-duty <target_mv>          compute duty%% for target voltage\n"
           "\n"
           "  cal ina <ch> v <gain> <offset_mv>  INA219 ch voltage correction\n"
           "  cal ina <ch> i <gain> <offset_ma>  INA219 ch current correction\n"
           "\n"
           "  cal adc <ch> <gain> <offset_mv>    ADC128D818 channel correction\n"
           "\n"
           "Example — migrate VDUT calibration from config:\n"
           "  cal vdut -87 11200\n"
           "  cal save\n"
           "  cal vdut-duty 3300   → should print ~90%%\n");
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
