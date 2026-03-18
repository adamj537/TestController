/* storage_spiffs.c — SPIFFS-backed recipe storage.
 *
 * Mounts the "recipes" partition (type=spiffs) at /recipes using ESP-IDF's
 * built-in SPIFFS VFS driver. No external dependencies required.
 * Implements the storage.h API for recipe read/write/list/delete.
 */

#include "storage.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>

static const char *TAG = "storage";
static bool s_mounted;

#define MOUNT_POINT "/recipes"
#define PARTITION   "recipes"

/* ── Init / Deinit ───────────────────────────────────────────────────────── */

int Storage_Init(void)
{
    if (s_mounted) return 0;

    esp_vfs_spiffs_conf_t cfg = {
        .base_path = MOUNT_POINT,
        .partition_label = PARTITION,
        .max_files = 10,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return -1;
    }
    s_mounted = true;

    size_t total = 0, used = 0;
    esp_spiffs_info(PARTITION, &total, &used);
    ESP_LOGI(TAG, "SPIFFS mounted at %s  total=%uKB  used=%uKB",
             MOUNT_POINT, (unsigned)(total / 1024), (unsigned)(used / 1024));
    return 0;
}

int Storage_Deinit(void)
{
    if (!s_mounted) return 0;
    esp_vfs_spiffs_unregister(PARTITION);
    s_mounted = false;
    return 0;
}

int Storage_GetInfo(StorageType_t type, StorageInfo_t *info)
{
    (void)type;
    if (!info || !s_mounted) return -1;

    size_t total = 0, used = 0;
    esp_spiffs_info(PARTITION, &total, &used);
    info->type = STORAGE_TYPE_NVS;
    info->available = true;
    info->total_bytes = (uint32_t)total;
    info->free_bytes = (uint32_t)(total - used);
    return 0;
}

/* ── Path helper ─────────────────────────────────────────────────────────── */

static void make_path(char *buf, size_t bufsz, const char *key)
{
    snprintf(buf, bufsz, MOUNT_POINT "/%s.json", key);
}

/* ── Read ────────────────────────────────────────────────────────────────── */

int32_t Storage_Read(StorageDomain_t domain, const char *key,
                     uint8_t *buffer, size_t buffer_size)
{
    (void)domain;
    if (!s_mounted || !key || !buffer) return -1;

    char path[296];
    make_path(path, sizeof(path), key);

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    int32_t n = (int32_t)fread(buffer, 1, buffer_size, f);
    fclose(f);
    return n;
}

int32_t Storage_ReadString(StorageDomain_t domain, const char *key,
                           char *buffer, size_t buffer_size)
{
    int32_t n = Storage_Read(domain, key, (uint8_t *)buffer, buffer_size - 1);
    if (n > 0) buffer[n] = '\0';
    return n;
}

/* ── Write ───────────────────────────────────────────────────────────────── */

int Storage_Write(StorageDomain_t domain, const char *key,
                  const uint8_t *data, size_t data_size, bool backup_to_sd)
{
    (void)domain;
    (void)backup_to_sd;
    if (!s_mounted || !key || !data) return -1;

    char path[296];
    make_path(path, sizeof(path), key);

    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGW(TAG, "Failed to open %s for writing", path);
        return -1;
    }
    size_t written = fwrite(data, 1, data_size, f);
    fclose(f);

    if (written != data_size) {
        ESP_LOGW(TAG, "Write incomplete: %d of %d bytes", (int)written, (int)data_size);
        return -1;
    }
    ESP_LOGI(TAG, "Wrote %s (%d bytes)", path, (int)data_size);
    return 0;
}

int Storage_WriteString(StorageDomain_t domain, const char *key,
                        const char *str, bool backup_to_sd)
{
    return Storage_Write(domain, key, (const uint8_t *)str, strlen(str), backup_to_sd);
}

/* ── Delete ──────────────────────────────────────────────────────────────── */

int Storage_Delete(StorageDomain_t domain, const char *key)
{
    (void)domain;
    if (!s_mounted || !key) return -1;

    char path[296];
    make_path(path, sizeof(path), key);
    return (remove(path) == 0) ? 0 : -1;
}

int Storage_ClearDomain(StorageDomain_t domain)
{
    (void)domain;
    if (!s_mounted) return -1;

    DIR *dir = opendir(MOUNT_POINT);
    if (!dir) return -1;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        char path[296];
        snprintf(path, sizeof(path), MOUNT_POINT "/%s", ent->d_name);
        remove(path);
    }
    closedir(dir);
    return 0;
}

/* ── List / Query ────────────────────────────────────────────────────────── */

int32_t Storage_ListKeys(StorageDomain_t domain, const char **keys,
                         uint32_t max_keys)
{
    (void)domain;
    if (!s_mounted || !keys) return -1;

    DIR *dir = opendir(MOUNT_POINT);
    if (!dir) return 0;

    int32_t count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && (uint32_t)count < max_keys) {
        /* Strip .json extension for display */
        char *dot = strrchr(ent->d_name, '.');
        if (dot && strcmp(dot, ".json") == 0) {
            size_t namelen = (size_t)(dot - ent->d_name);
            char *name = malloc(namelen + 1);
            if (name) {
                memcpy(name, ent->d_name, namelen);
                name[namelen] = '\0';
                keys[count++] = name;
            }
        }
    }
    closedir(dir);
    return count;
}

int Storage_Commit(void) { return 0; }

int Storage_Exists(StorageDomain_t domain, const char *key)
{
    (void)domain;
    if (!s_mounted || !key) return -1;

    char path[296];
    make_path(path, sizeof(path), key);

    struct stat st;
    return (stat(path, &st) == 0) ? 1 : 0;
}

int32_t Storage_GetSize(StorageDomain_t domain, const char *key)
{
    (void)domain;
    if (!s_mounted || !key) return -1;

    char path[296];
    make_path(path, sizeof(path), key);

    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (int32_t)st.st_size;
}
