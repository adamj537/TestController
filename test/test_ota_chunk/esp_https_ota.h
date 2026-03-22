/* esp_https_ota.h — native test stub */
#pragma once
#include "esp_console.h"
#include "esp_http_client.h"

typedef struct {
    const esp_http_client_config_t *http_config;
    esp_err_t (*http_client_init_cb)(esp_http_client_handle_t);
} esp_https_ota_config_t;

static inline esp_err_t esp_https_ota(const esp_https_ota_config_t *c) { (void)c; return 0; }
