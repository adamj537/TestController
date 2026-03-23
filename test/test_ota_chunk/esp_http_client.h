/* esp_http_client.h — native test stub */
#pragma once
#include "esp_console.h"
typedef void *esp_http_client_handle_t;
typedef struct {
    const char *url;
    void       *crt_bundle_attach;
    const char *cert_pem;
    int         timeout_ms;
    int         keep_alive_enable;
} esp_http_client_config_t;
static inline esp_err_t esp_http_client_set_header(esp_http_client_handle_t c,
                                                    const char *k, const char *v)
{ (void)c; (void)k; (void)v; return 0; }
static inline void *esp_crt_bundle_attach(void *c) { (void)c; return NULL; }
