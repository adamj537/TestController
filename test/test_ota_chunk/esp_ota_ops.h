/* esp_ota_ops.h — native test stub */
#pragma once
#include "esp_console.h"   /* for esp_err_t */
#include <stddef.h>
#include <stdint.h>

typedef uint32_t esp_ota_handle_t;

#define OTA_WITH_SEQUENTIAL_WRITES 0

typedef struct {
    const char *label;
} esp_partition_t;

typedef enum {
    ESP_OTA_IMG_NEW            = 0,
    ESP_OTA_IMG_PENDING_VERIFY = 1,
    ESP_OTA_IMG_VALID          = 2,
    ESP_OTA_IMG_INVALID        = 3,
    ESP_OTA_IMG_ABORTED        = 4,
    ESP_OTA_IMG_UNDEFINED      = 0xFF,
} esp_ota_img_states_t;

/* ── Mock control — set by tests ── */
extern int  g_mock_ota_begin_rc;
extern int  g_mock_ota_write_rc;
extern int  g_mock_ota_end_rc;
extern int  g_mock_ota_set_boot_rc;
extern int  g_mock_ota_abort_called;
extern int  g_mock_ota_end_called;
extern int  g_mock_ota_set_boot_called;
extern int  g_mock_ota_write_count;
extern int  g_mock_no_partition;

static inline const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *p)
{
    (void)p;
    if (g_mock_no_partition) return NULL;
    static esp_partition_t part = { .label = "ota_1" };
    return &part;
}

static inline esp_err_t esp_ota_begin(const esp_partition_t *p, int size,
                                       esp_ota_handle_t *out)
{
    (void)p; (void)size;
    *out = 1;   /* non-zero handle */
    return g_mock_ota_begin_rc;
}

static inline esp_err_t esp_ota_write(esp_ota_handle_t h,
                                       const void *data, size_t size)
{
    (void)h; (void)data; (void)size;
    g_mock_ota_write_count++;
    return g_mock_ota_write_rc;
}

static inline esp_err_t esp_ota_end(esp_ota_handle_t h)
{
    (void)h;
    g_mock_ota_end_called++;
    return g_mock_ota_end_rc;
}

static inline esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p)
{
    (void)p;
    g_mock_ota_set_boot_called++;
    return g_mock_ota_set_boot_rc;
}

static inline esp_err_t esp_ota_abort(esp_ota_handle_t h)
{
    (void)h;
    g_mock_ota_abort_called++;
    return ESP_OK;
}

static inline const esp_partition_t *esp_ota_get_running_partition(void)
{
    static esp_partition_t part = { .label = "ota_0" };
    return &part;
}

static inline esp_err_t esp_ota_get_state_partition(const esp_partition_t *p,
                                                     esp_ota_img_states_t *s)
{
    (void)p;
    *s = ESP_OTA_IMG_VALID;
    return ESP_OK;
}

static inline esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) { return ESP_OK; }
static inline esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void) { return ESP_OK; }
