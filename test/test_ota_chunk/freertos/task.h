/* freertos/task.h — native test stub */
#pragma once
#include <stdint.h>
typedef void *TaskHandle_t;
typedef uint32_t UBaseType_t;
typedef uint32_t StackType_t;
static inline void vTaskDelay(uint32_t ticks) { (void)ticks; }
static inline void vTaskDelete(void *h) { (void)h; }
static inline int xTaskCreate(void (*fn)(void *), const char *name,
                               uint32_t stack, void *arg,
                               UBaseType_t prio, TaskHandle_t *out)
{
    (void)fn; (void)name; (void)stack; (void)arg; (void)prio; (void)out;
    return 1; /* pdPASS */
}
