/* esp_console.h — native test stub */
#pragma once
#include <stddef.h>
typedef int esp_err_t;
#ifndef ESP_OK
#define ESP_OK 0
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
