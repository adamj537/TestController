/* esp_app_desc.h — native test stub */
#pragma once
#include <stdint.h>
typedef struct {
    char version[32];
    char date[16];
    char time[16];
    char idf_ver[32];
} esp_app_desc_t;
static inline const esp_app_desc_t *esp_app_get_description(void) {
    static esp_app_desc_t d = { "1.0.0", "2026-01-01", "00:00:00", "v5.0" };
    return &d;
}
