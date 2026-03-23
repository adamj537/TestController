/* esp_console.h — native test stub */
#pragma once
#include <stddef.h>
typedef int esp_err_t;
#ifndef ESP_OK
#define ESP_OK 0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL (-1)
#endif
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG 0x102
#endif
#ifndef ESP_ERR_NO_MEM
#define ESP_ERR_NO_MEM 0x101
#endif
#ifndef ESP_ERR_INVALID_SIZE
#define ESP_ERR_INVALID_SIZE 0x103
#endif
#ifndef ESP_ERR_OTA_VALIDATE_FAILED
#define ESP_ERR_OTA_VALIDATE_FAILED 0x105
#endif
typedef struct {
    const char *command;
    const char *help;
    const char *hint;
    int (*func)(int, char **);
    void *argtable;
} esp_console_cmd_t;
static inline esp_err_t esp_console_cmd_register(const esp_console_cmd_t *c) {
    (void)c;
    return ESP_OK;
}
#ifndef ESP_ERROR_CHECK
#define ESP_ERROR_CHECK(x) ((void)(x))
#endif

static inline const char *esp_err_to_name(esp_err_t err)
{
    (void)err;
    return "ESP_ERR";
}
