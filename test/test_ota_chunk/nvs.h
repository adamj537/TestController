/* nvs.h — native test stub */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY = 0, NVS_READWRITE = 1 } nvs_open_mode_t;

static inline int nvs_open(const char *ns, nvs_open_mode_t m, nvs_handle_t *h)
{ (void)ns; (void)m; *h = 0; return -1; }
static inline int nvs_get_str(nvs_handle_t h, const char *k, char *v, size_t *l)
{ (void)h; (void)k; (void)v; (void)l; return -1; }
static inline int nvs_set_str(nvs_handle_t h, const char *k, const char *v)
{ (void)h; (void)k; (void)v; return -1; }
static inline int nvs_commit(nvs_handle_t h) { (void)h; return 0; }
static inline void nvs_close(nvs_handle_t h) { (void)h; }
