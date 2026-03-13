/**
 * @file hal_system_esp32_mock.c
 * @brief Mock ESP32 System HAL for Linux testing
 *
 * Simulates timing and system control without ESP-IDF dependencies.
 * Allows Recipe execution testing on Linux.
 */

#include "../hal_system.h"
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ==================== Mock State ==================== */

typedef struct {
    bool initialized;
    uint64_t start_micros;  /* Microseconds at boot */
    uint32_t heap_free;     /* Simulated free heap */
} System_Mock_t;

static System_Mock_t system_mock = {0};

/* Mock debug logging */
#ifdef DEBUG
#define SYSTEM_LOG(fmt, ...) printf("[SYSTEM] " fmt "\n", ##__VA_ARGS__)
#else
#define SYSTEM_LOG(fmt, ...) do {} while(0)
#endif

/* ==================== Helper: Get current time in microseconds ==================== */

static uint64_t get_micros(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

/* ==================== Initialization ==================== */

int HAL_System_Init(void) {
    if (system_mock.initialized) return 0;

    system_mock.start_micros = get_micros();
    system_mock.heap_free = 1024 * 1024;  /* Simulate 1 MB free */
    system_mock.initialized = true;

    SYSTEM_LOG("Init");
    return 0;
}

/* ==================== Timekeeping ==================== */

uint32_t HAL_GetTick(void) {
    if (!system_mock.initialized) return 0;

    uint64_t current = get_micros();
    uint64_t elapsed = current - system_mock.start_micros;
    uint32_t ms = (uint32_t)(elapsed / 1000);

    SYSTEM_LOG("GetTick = %u ms", ms);
    return ms;
}

uint64_t HAL_GetMicros(void) {
    if (!system_mock.initialized) return 0;

    uint64_t current = get_micros();
    uint64_t elapsed = current - system_mock.start_micros;

    SYSTEM_LOG("GetMicros = %llu us", (unsigned long long)elapsed);
    return elapsed;
}

/* ==================== Delays ==================== */

void HAL_Delay(uint32_t ms) {
    if (!system_mock.initialized) return;

    SYSTEM_LOG("Delay %u ms", ms);
    usleep(ms * 1000);
}

void HAL_DelayMicros(uint32_t us) {
    if (!system_mock.initialized) return;

    SYSTEM_LOG("DelayMicros %u us", us);
    usleep(us);
}

/* ==================== System Control ==================== */

void HAL_System_Reset(void) {
    SYSTEM_LOG("Reset requested (not actually resetting in mock)");
    exit(1);
}

int HAL_System_Sleep(uint32_t ms) {
    if (!system_mock.initialized) return -1;

    SYSTEM_LOG("Sleep %u ms", ms);
    usleep(ms * 1000);
    return 0;
}

int HAL_System_Wake(void) {
    if (!system_mock.initialized) return -1;

    SYSTEM_LOG("Wake");
    return 0;
}

/* ==================== System Information ==================== */

int HAL_System_GetChipId(uint8_t* buffer, size_t buffer_size) {
    if (!buffer || buffer_size < 6) return -1;

    /* Simulate a MAC address as chip ID */
    buffer[0] = 0xAA;
    buffer[1] = 0xBB;
    buffer[2] = 0xCC;
    buffer[3] = 0xDD;
    buffer[4] = 0xEE;
    buffer[5] = 0xFF;

    SYSTEM_LOG("GetChipId: %02X:%02X:%02X:%02X:%02X:%02X", buffer[0], buffer[1], buffer[2],
               buffer[3], buffer[4], buffer[5]);
    return 6;
}

const char* HAL_System_GetFirmwareVersion(void) {
    return "TC-FW-1.0-Mock-Linux";
}

uint32_t HAL_System_GetFreeHeap(void) {
    if (!system_mock.initialized) return 0;

    SYSTEM_LOG("GetFreeHeap = %u bytes", system_mock.heap_free);
    return system_mock.heap_free;
}

/* ==================== Mock Helpers (for testing) ==================== */

/**
 * @brief Set simulated free heap
 */
void HAL_System_Mock_SetFreeHeap(uint32_t bytes) {
    system_mock.heap_free = bytes;
    SYSTEM_LOG("Mock: Set free heap to %u bytes", bytes);
}

/**
 * @brief Simulate heap allocation (decreases free heap)
 */
void HAL_System_Mock_AllocateHeap(uint32_t bytes) {
    if (system_mock.heap_free >= bytes) {
        system_mock.heap_free -= bytes;
    }
    SYSTEM_LOG("Mock: Allocate %u bytes (free now: %u)", bytes, system_mock.heap_free);
}

/**
 * @brief Simulate heap deallocation (increases free heap)
 */
void HAL_System_Mock_FreeHeap(uint32_t bytes) {
    system_mock.heap_free += bytes;
    SYSTEM_LOG("Mock: Free %u bytes (free now: %u)", bytes, system_mock.heap_free);
}

/**
 * @brief Reset all mock state
 */
void HAL_System_Mock_Reset(void) {
    system_mock.start_micros = get_micros();
    system_mock.heap_free = 1024 * 1024;
    SYSTEM_LOG("Reset all System mock state");
}
