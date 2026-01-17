/**
 * @file hal_system.h
 * @brief System Hardware Abstraction Layer
 *
 * Timekeeping, delays, and system control.
 * Implementations: hal_system_esp32.c, hal_system_mock.c
 */

#ifndef HAL_SYSTEM_H
#define HAL_SYSTEM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Core System Operations ==================== */

/**
 * @brief Initialize system (clock, timers, etc.)
 *
 * Must be called once at startup.
 *
 * @return 0 on success, -1 on error
 */
int HAL_System_Init(void);

/**
 * @brief Get milliseconds since boot
 *
 * @return Milliseconds (uint32_t, wraps after ~49 days)
 */
uint32_t HAL_GetTick(void);

/**
 * @brief Get microseconds since boot
 *
 * @return Microseconds (uint64_t)
 */
uint64_t HAL_GetMicros(void);

/**
 * @brief Busy-wait delay
 *
 * @param ms Milliseconds to delay
 */
void HAL_Delay(uint32_t ms);

/**
 * @brief Delay with microsecond precision
 *
 * @param us Microseconds to delay
 */
void HAL_DelayMicros(uint32_t us);

/* ==================== System Control ==================== */

/**
 * @brief Perform software reset
 *
 * Does not return.
 */
void HAL_System_Reset(void);

/**
 * @brief Enter sleep mode (power saving)
 *
 * @param ms Duration to sleep in milliseconds
 *
 * @return 0 on success, -1 on error
 */
int HAL_System_Sleep(uint32_t ms);

/**
 * @brief Wake from sleep
 *
 * @return 0 on success, -1 on error
 */
int HAL_System_Wake(void);

/* ==================== System Information ==================== */

/**
 * @brief Get chip ID or unique identifier
 *
 * @param buffer Output buffer
 * @param buffer_size Size of buffer
 *
 * @return Number of bytes written, or -1 on error
 */
int HAL_System_GetChipId(uint8_t* buffer, size_t buffer_size);

/**
 * @brief Get firmware version string
 *
 * @return Pointer to version string (do not free)
 */
const char* HAL_System_GetFirmwareVersion(void);

/**
 * @brief Get free heap memory
 *
 * @return Free memory in bytes
 */
uint32_t HAL_System_GetFreeHeap(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SYSTEM_H */
