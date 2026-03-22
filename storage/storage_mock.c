/**
 * @file storage_mock.c
 * @brief Mock storage implementation for unit testing
 *
 * Simulates NVS and SD card storage in RAM for off-board testing.
 * Includes test helpers for verification.
 *
 * NOT compiled for embedded targets — guard prevents 1MB BSS from landing
 * in firmware. Included directly by test files via #include.
 */
#ifndef ESP_PLATFORM

#include "storage.h"
#include <string.h>
#include <stdlib.h>

/* ==================== Mock Storage State ==================== */

#define MOCK_STORAGE_ENTRIES 100
#define MOCK_STORAGE_SIZE    (1024 * 1024)  /* 1MB simulated storage (accommodate large Recipe_t structs) */

typedef struct {
    char key[64];
    StorageDomain_t domain;
    uint8_t value[32768]; /* Sized to hold recipe JSON (~16KB for g3-mb-v2) */
    uint32_t size;
    bool valid;
} StorageEntry_t;

static struct {
    bool initialized;
    StorageEntry_t entries[MOCK_STORAGE_ENTRIES];
    uint32_t used_bytes;
    uint32_t entry_count;
} mock_storage = {false, {}, 0, 0};

/* ==================== Initialization ==================== */

int Storage_Init(void) {
    if (mock_storage.initialized) return 0;

    /* If re-initializing after Deinit, preserve existing entries (simulate NVS persistence).
     * Only clear if never initialized (first call in a process lifetime). */
    if (mock_storage.entry_count == 0 && mock_storage.used_bytes == 0) {
        memset(&mock_storage, 0, sizeof(mock_storage));
    }
    mock_storage.initialized = true;
    return 0;
}

int Storage_Deinit(void) {
    mock_storage.initialized = false;
    return 0;
}

int Storage_GetInfo(StorageType_t type, StorageInfo_t* info) {
    if (!mock_storage.initialized || !info) return -1;

    if (type == STORAGE_TYPE_NVS) {
        info->type = STORAGE_TYPE_NVS;
        info->available = true;
        info->total_bytes = MOCK_STORAGE_SIZE;
        info->free_bytes = MOCK_STORAGE_SIZE - mock_storage.used_bytes;
        return 0;
    }

    return -1;  /* SD card not available in mock */
}

/* ==================== Read Operations ==================== */

int32_t Storage_Read(StorageDomain_t domain, const char* key,
                     uint8_t* buffer, size_t buffer_size) {
    if (!mock_storage.initialized || !key || !buffer) return -1;

    for (uint32_t i = 0; i < mock_storage.entry_count; i++) {
        StorageEntry_t* entry = &mock_storage.entries[i];
        if (entry->valid && entry->domain == domain &&
            strcmp(entry->key, key) == 0) {
            uint32_t copy_size = entry->size;
            if (copy_size > buffer_size) copy_size = buffer_size;
            memcpy(buffer, entry->value, copy_size);
            return copy_size;
        }
    }

    return 0;  /* Key not found */
}

int32_t Storage_ReadString(StorageDomain_t domain, const char* key,
                           char* buffer, size_t buffer_size) {
    if (!buffer_size) return -1;

    int32_t bytes = Storage_Read(domain, key, (uint8_t*)buffer, buffer_size - 1);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        return bytes + 1;  /* Include NUL terminator */
    }

    if (bytes == 0) {
        buffer[0] = '\0';
        return 0;
    }

    return -1;
}

int32_t Storage_ListKeys(StorageDomain_t domain, const char** keys,
                         uint32_t max_keys) {
    if (!mock_storage.initialized) return -1;

    uint32_t count = 0;
    for (uint32_t i = 0; i < mock_storage.entry_count && count < max_keys; i++) {
        if (mock_storage.entries[i].valid && mock_storage.entries[i].domain == domain) {
            keys[count] = mock_storage.entries[i].key;
            count++;
        }
    }

    return count;
}

/* ==================== Write Operations ==================== */

int Storage_Write(StorageDomain_t domain, const char* key,
                  const uint8_t* data, size_t data_size,
                  bool backup_to_sd) {
    if (!mock_storage.initialized || !key || !data || data_size > 32768) {
        return -1;
    }

    /* Check if key already exists */
    for (uint32_t i = 0; i < mock_storage.entry_count; i++) {
        if (mock_storage.entries[i].valid &&
            mock_storage.entries[i].domain == domain &&
            strcmp(mock_storage.entries[i].key, key) == 0) {
            /* Update existing */
            mock_storage.used_bytes -= mock_storage.entries[i].size;
            memcpy(mock_storage.entries[i].value, data, data_size);
            mock_storage.entries[i].size = data_size;
            mock_storage.used_bytes += data_size;
            return 0;
        }
    }

    /* Add new entry */
    if (mock_storage.entry_count >= MOCK_STORAGE_ENTRIES) {
        return -1;  /* Storage full */
    }

    if (mock_storage.used_bytes + data_size > MOCK_STORAGE_SIZE) {
        return -1;  /* Not enough space */
    }

    StorageEntry_t* entry = &mock_storage.entries[mock_storage.entry_count];
    strncpy(entry->key, key, sizeof(entry->key) - 1);
    entry->key[sizeof(entry->key) - 1] = '\0';
    entry->domain = domain;
    memcpy(entry->value, data, data_size);
    entry->size = data_size;
    entry->valid = true;

    mock_storage.used_bytes += data_size;
    mock_storage.entry_count++;

    return 0;
}

int Storage_WriteString(StorageDomain_t domain, const char* key,
                        const char* str, bool backup_to_sd) {
    if (!str) return -1;
    return Storage_Write(domain, key, (const uint8_t*)str,
                        strlen(str) + 1, backup_to_sd);
}

/* ==================== Deletion ==================== */

int Storage_Delete(StorageDomain_t domain, const char* key) {
    if (!mock_storage.initialized || !key) return -1;

    for (uint32_t i = 0; i < mock_storage.entry_count; i++) {
        if (mock_storage.entries[i].valid &&
            mock_storage.entries[i].domain == domain &&
            strcmp(mock_storage.entries[i].key, key) == 0) {
            mock_storage.used_bytes -= mock_storage.entries[i].size;
            mock_storage.entries[i].valid = false;
            return 0;
        }
    }

    return -1;  /* Key not found */
}

int Storage_ClearDomain(StorageDomain_t domain) {
    if (!mock_storage.initialized) return -1;

    for (uint32_t i = 0; i < mock_storage.entry_count; i++) {
        if (mock_storage.entries[i].valid && mock_storage.entries[i].domain == domain) {
            mock_storage.used_bytes -= mock_storage.entries[i].size;
            mock_storage.entries[i].valid = false;
        }
    }

    return 0;
}

/* ==================== Advanced ==================== */

int Storage_Commit(void) {
    /* Mock has no buffers, so nothing to commit */
    return 0;
}

int Storage_Exists(StorageDomain_t domain, const char* key) {
    if (!mock_storage.initialized || !key) return -1;

    for (uint32_t i = 0; i < mock_storage.entry_count; i++) {
        if (mock_storage.entries[i].valid &&
            mock_storage.entries[i].domain == domain &&
            strcmp(mock_storage.entries[i].key, key) == 0) {
            return 1;
        }
    }

    return 0;
}

int32_t Storage_GetSize(StorageDomain_t domain, const char* key) {
    if (!mock_storage.initialized || !key) return -1;

    for (uint32_t i = 0; i < mock_storage.entry_count; i++) {
        if (mock_storage.entries[i].valid &&
            mock_storage.entries[i].domain == domain &&
            strcmp(mock_storage.entries[i].key, key) == 0) {
            return mock_storage.entries[i].size;
        }
    }

    return 0;
}

/* ==================== Mock Test Helpers ==================== */

/**
 * @brief Reset mock storage (for test isolation)
 */
void Storage_Mock_Reset(void) {
    memset(&mock_storage, 0, sizeof(mock_storage));
}

/**
 * @brief Get mock storage usage statistics
 *
 * @param used_bytes Pointer to receive used bytes
 * @param total_bytes Pointer to receive total bytes
 */
void Storage_Mock_GetStats(uint32_t* used_bytes, uint32_t* total_bytes) {
    if (used_bytes) *used_bytes = mock_storage.used_bytes;
    if (total_bytes) *total_bytes = MOCK_STORAGE_SIZE;
}

/**
 * @brief Get number of entries in storage
 */
uint32_t Storage_Mock_GetEntryCount(void) {
    return mock_storage.entry_count;
}

#endif /* ESP_PLATFORM */
