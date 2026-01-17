/**
 * @file storage.h
 * @brief Storage abstraction layer for recipes, calibration, and configuration
 *
 * Provides unified interface for NVS (flash) and optional SD card storage.
 * Implementations: storage_nvs.c (primary), storage_sd.c (fallback)
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Type Definitions ==================== */

typedef enum {
    STORAGE_TYPE_NVS,   /* Flash-based (always available) */
    STORAGE_TYPE_SD     /* SD card (optional) */
} StorageType_t;

typedef enum {
    STORAGE_DOMAIN_RECIPES,      /* Test recipes */
    STORAGE_DOMAIN_CALIBRATION,  /* Calibration data */
    STORAGE_DOMAIN_CONFIGURATION /* Config/state */
} StorageDomain_t;

typedef struct {
    StorageType_t type;
    bool available;
    uint32_t total_bytes;
    uint32_t free_bytes;
} StorageInfo_t;

/* ==================== Initialization ==================== */

/**
 * @brief Initialize storage subsystem
 *
 * Attempts NVS first, then SD card if available.
 * At least one must succeed.
 *
 * @return 0 on success (at least one storage type available), -1 on error
 */
int Storage_Init(void);

/**
 * @brief Deinitialize storage subsystem
 *
 * @return 0 on success, -1 on error
 */
int Storage_Deinit(void);

/**
 * @brief Get storage information
 *
 * @param type Storage type to query
 * @param info Pointer to info structure
 *
 * @return 0 on success, -1 if storage type not available
 */
int Storage_GetInfo(StorageType_t type, StorageInfo_t* info);

/* ==================== Read Operations ==================== */

/**
 * @brief Read data from storage
 *
 * Tries preferred storage type first, falls back to other if available.
 *
 * @param domain Storage domain
 * @param key Key name (max 64 chars)
 * @param buffer Buffer to read data into
 * @param buffer_size Size of buffer
 *
 * @return Bytes read, 0 if key not found, -1 on error
 */
int32_t Storage_Read(StorageDomain_t domain, const char* key,
                     uint8_t* buffer, size_t buffer_size);

/**
 * @brief Read string from storage
 *
 * Convenience function for NUL-terminated strings.
 *
 * @param domain Storage domain
 * @param key Key name
 * @param buffer Buffer for string
 * @param buffer_size Size of buffer
 *
 * @return Bytes read (including NUL terminator), 0 if not found, -1 on error
 */
int32_t Storage_ReadString(StorageDomain_t domain, const char* key,
                           char* buffer, size_t buffer_size);

/**
 * @brief List all keys in a domain
 *
 * Useful for enumerating recipes.
 *
 * @param domain Storage domain
 * @param keys Array to receive key names
 * @param max_keys Maximum number of keys to return
 *
 * @return Number of keys found, -1 on error
 */
int32_t Storage_ListKeys(StorageDomain_t domain, const char** keys,
                         uint32_t max_keys);

/* ==================== Write Operations ==================== */

/**
 * @brief Write data to storage
 *
 * Writes to primary storage type (NVS), with optional SD backup.
 *
 * @param domain Storage domain
 * @param key Key name (max 64 chars)
 * @param data Data to write
 * @param data_size Size of data
 * @param backup_to_sd If true, also write to SD card
 *
 * @return 0 on success, -1 on error
 */
int Storage_Write(StorageDomain_t domain, const char* key,
                  const uint8_t* data, size_t data_size,
                  bool backup_to_sd);

/**
 * @brief Write string to storage
 *
 * Convenience function for NUL-terminated strings.
 *
 * @param domain Storage domain
 * @param key Key name
 * @param str String to write
 * @param backup_to_sd If true, also write to SD card
 *
 * @return 0 on success, -1 on error
 */
int Storage_WriteString(StorageDomain_t domain, const char* key,
                        const char* str, bool backup_to_sd);

/* ==================== Deletion ==================== */

/**
 * @brief Delete a key from storage
 *
 * @param domain Storage domain
 * @param key Key name
 *
 * @return 0 on success, -1 if key not found or error
 */
int Storage_Delete(StorageDomain_t domain, const char* key);

/**
 * @brief Clear all keys in a domain
 *
 * @param domain Storage domain
 *
 * @return 0 on success, -1 on error
 */
int Storage_ClearDomain(StorageDomain_t domain);

/* ==================== Advanced ==================== */

/**
 * @brief Commit changes to persistent storage
 *
 * For NVS, writes dirty pages to flash.
 * For SD, flushes buffers to disk.
 *
 * @return 0 on success, -1 on error
 */
int Storage_Commit(void);

/**
 * @brief Check if a key exists in storage
 *
 * @param domain Storage domain
 * @param key Key name
 *
 * @return 1 if exists, 0 if not found, -1 on error
 */
int Storage_Exists(StorageDomain_t domain, const char* key);

/**
 * @brief Get size of a stored value
 *
 * @param domain Storage domain
 * @param key Key name
 *
 * @return Size in bytes, 0 if not found, -1 on error
 */
int32_t Storage_GetSize(StorageDomain_t domain, const char* key);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_H */
