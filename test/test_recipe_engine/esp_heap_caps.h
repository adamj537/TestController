#pragma once
/* Native test stub — esp_heap_caps not available on host */
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM  0
static inline void *heap_caps_malloc(size_t size, uint32_t caps) { (void)caps; return malloc(size); }
