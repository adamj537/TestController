/**
 * @file hal_system_esp32.c
 * @brief ESP32-specific System HAL implementation
 *
 * Uses ESP-IDF timer and system APIs for timing, delays, and system control.
 * Provides platform-independent timing interface.
 */

#include "../hal_system.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/* ==================== Configuration ==================== */

#define FW_VERSION "TC-FW-1.0-ESP32"

typedef struct {
    bool initialized;
} System_State_t;

static System_State_t system_state = {false};

/* ==================== Initialization ==================== */

int HAL_System_Init(void) {
    if (system_state.initialized) return 0;

    /* ESP32 system is initialized by FreeRTOS and esp_timer automatically */
    /* esp_timer is initialized before app_main() is called */

    system_state.initialized = true;
    return 0;
}

/* ==================== Timekeeping ==================== */

uint32_t HAL_GetTick(void) {
    /* Get milliseconds since boot using esp_timer */
    uint64_t micros = esp_timer_get_time();
    return (uint32_t)(micros / 1000);
}

uint64_t HAL_GetMicros(void) {
    /* Get microseconds since boot using esp_timer */
    return esp_timer_get_time();
}

/* ==================== Delays ==================== */

void HAL_Delay(uint32_t ms) {
    if (!system_state.initialized) return;

    /* Use FreeRTOS vTaskDelay for millisecond delays */
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

void HAL_DelayMicros(uint32_t us) {
    if (!system_state.initialized) return;

    /* For microsecond delays, use busy-wait via esp_rom_delay_us */
    /* This is non-blocking but should only be used for short delays */
    uint64_t start = esp_timer_get_time();
    uint64_t delay_us = (uint64_t)us;

    while ((esp_timer_get_time() - start) < delay_us) {
        /* Busy wait */
    }
}

/* ==================== System Control ==================== */

void HAL_System_Reset(void) {
    /* Perform software reset */
    esp_restart();
    /* Does not return */
}

int HAL_System_Sleep(uint32_t ms) {
    if (!system_state.initialized) return -1;

    /* Use FreeRTOS task delay for sleep */
    vTaskDelay(ms / portTICK_PERIOD_MS);

    return 0;
}

int HAL_System_Wake(void) {
    if (!system_state.initialized) return -1;

    /* Wake is implicit when delay completes */
    return 0;
}

/* ==================== System Information ==================== */

int HAL_System_GetChipId(uint8_t* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return -1;

    if (buffer_size < 6) return -1;

    /* Get ESP32 chip ID using esp_efuse API */
    uint32_t chip_id_low = 0;
    uint32_t chip_id_high = 0;

    /* Read the MAC address as unique ID (6 bytes) */
    esp_efuse_read_mac(buffer);

    return 6;  /* MAC address is 6 bytes */
}

const char* HAL_System_GetFirmwareVersion(void) {
    return FW_VERSION;
}

/* ==================== Memory ==================== */

uint32_t HAL_System_GetFreeHeap(void) {
    if (!system_state.initialized) return 0;

    /* Get free heap memory from both DRAM and PSRAM if available */
    uint32_t dram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    return dram_free + psram_free;
}
