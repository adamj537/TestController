/**
 * @file hal_system_mock.c
 * @brief System Mock Implementation for Unit Testing
 */

#include "../hal_system.h"
#include <time.h>
#include <string.h>

static struct {
    uint32_t tick_ms;
    uint64_t micros;
    bool initialized;
} system_mock = {0};

int HAL_System_Init(void) {
    system_mock.initialized = true;
    system_mock.tick_ms = 0;
    system_mock.micros = 0;
    return 0;
}

uint32_t HAL_GetTick(void) {
    if (!system_mock.initialized) return 0;
    return system_mock.tick_ms;
}

uint64_t HAL_GetMicros(void) {
    if (!system_mock.initialized) return 0;
    return system_mock.micros;
}

void HAL_Delay(uint32_t ms) {
    if (!system_mock.initialized) return;
    system_mock.tick_ms += ms;
    system_mock.micros += (uint64_t)ms * 1000;
}

void HAL_DelayMicros(uint32_t us) {
    if (!system_mock.initialized) return;
    system_mock.micros += us;
}

void HAL_System_Reset(void) {
    /* Mock doesn't actually reset */
}

int HAL_System_Sleep(uint32_t ms) {
    HAL_Delay(ms);
    return 0;
}

int HAL_System_Wake(void) {
    return 0;
}

int HAL_System_GetChipId(uint8_t* buffer, size_t buffer_size) {
    if (!buffer || buffer_size < 4) return -1;

    /* Return mock chip ID */
    memset(buffer, 0xAA, buffer_size);
    return buffer_size;
}

const char* HAL_System_GetFirmwareVersion(void) {
    return "FW-1 v1.0.0-mock";
}

uint32_t HAL_System_GetFreeHeap(void) {
    return 1024 * 1024;  /* Mock reports 1MB free */
}

/* Mock access for testing */
void HAL_System_Mock_SetTick(uint32_t ms) {
    system_mock.tick_ms = ms;
}

uint32_t HAL_System_Mock_GetTick(void) {
    return system_mock.tick_ms;
}

void HAL_System_Mock_Reset(void) {
    memset(&system_mock, 0, sizeof(system_mock));
}
