/* storage_spiffs.c — LittleFS-backed recipe and offline-buffer storage.
 *
 * Mounts the "recipes" partition at /recipes using LittleFS (joltwallet/esp_littlefs).
 * Subdirectories:
 *   /recipes/      — named recipe JSON files ({key}.json)
 *   /offline/      — queued result payloads for offline buffering
 *
 * OFFLINE_BUF_CAPACITY: compile-time limit on buffered offline results.
 * The 384 KB "recipes" partition comfortably holds 80 compressed result payloads.
 *
 * Partition subtype in the CSV remains "spiffs" (0x82) — LittleFS uses the
 * partition label, not the subtype, so no partition table change is needed.
 * The filesystem is reformatted automatically on first boot (format_if_mount_failed).
 * Re-push recipes after firmware update.
 */
#define OFFLINE_BUF_CAPACITY  80

#include "storage.h"
#include "esp_littlefs.h"
#include "nvs.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>

/* ── NVS-backed calibration domain ───────────────────────────────────────── *
 * STORAGE_DOMAIN_CALIBRATION is routed to NVS (namespace "cal") rather than  *
 * SPIFFS.  This keeps calibration data on a separate storage medium that is   *
 * independent from the recipe SPIFFS partition and survives format operations.*
 * nvs_flash_init() must already have been called before Storage_Init().       */
#define CAL_NVS_NS  "cal"

static int nvs_cal_read(const char *key, uint8_t *buf, size_t bufsz)
{
    nvs_handle_t h;
    if (nvs_open(CAL_NVS_NS, NVS_READONLY, &h) != ESP_OK) return 0;
    size_t sz = bufsz;
    esp_err_t err = nvs_get_blob(h, key, buf, &sz);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return 0;
    return (err == ESP_OK) ? (int)sz : -1;
}

static int nvs_cal_size(const char *key)
{
    nvs_handle_t h;
    if (nvs_open(CAL_NVS_NS, NVS_READONLY, &h) != ESP_OK) return 0;
    size_t sz = 0;
    esp_err_t err = nvs_get_blob(h, key, NULL, &sz);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return 0;
    return (err == ESP_OK || err == ESP_ERR_NVS_INVALID_LENGTH) ? (int)sz : -1;
}

static int nvs_cal_write(const char *key, const uint8_t *data, size_t data_size)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CAL_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE("storage", "cal NVS open failed: %s", esp_err_to_name(err));
        return -1;
    }
    err = nvs_set_blob(h, key, data, data_size);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE("storage", "cal NVS write failed: %s", esp_err_to_name(err));
        return -1;
    }
    return 0;
}

static int nvs_cal_delete(const char *key)
{
    nvs_handle_t h;
    if (nvs_open(CAL_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return -1;
    esp_err_t err = nvs_erase_key(h, key);
    if (err == ESP_OK) nvs_commit(h);
    nvs_close(h);
    return (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) ? 0 : -1;
}

static const char *TAG = "storage";
static bool s_mounted;

#define MOUNT_POINT "/recipes"
#define PARTITION   "recipes"

/* ── Init / Deinit ───────────────────────────────────────────────────────── */

int Storage_Init(void)
{
    if (s_mounted) return 0;

    esp_vfs_littlefs_conf_t cfg = {
        .base_path = MOUNT_POINT,
        .partition_label = PARTITION,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(err));
        return -1;
    }
    s_mounted = true;

    /* Create subdirectories on first mount (LittleFS requires explicit mkdir). */
    mkdir(MOUNT_POINT "/offline", 0755);

    size_t total = 0, used = 0;
    esp_littlefs_info(PARTITION, &total, &used);
    ESP_LOGI(TAG, "LittleFS mounted at %s  total=%uKB  used=%uKB  offline_cap=%d",
             MOUNT_POINT, (unsigned)(total / 1024), (unsigned)(used / 1024),
             OFFLINE_BUF_CAPACITY);
    return 0;
}

int Storage_Deinit(void)
{
    if (!s_mounted) return 0;
    esp_vfs_littlefs_unregister(PARTITION);
    s_mounted = false;
    return 0;
}

int Storage_GetInfo(StorageType_t type, StorageInfo_t *info)
{
    (void)type;
    if (!info || !s_mounted) return -1;

    size_t total = 0, used = 0;
    esp_littlefs_info(PARTITION, &total, &used);
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
    if (domain == STORAGE_DOMAIN_CALIBRATION) return nvs_cal_read(key, buffer, buffer_size);
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
    (void)backup_to_sd;
    if (domain == STORAGE_DOMAIN_CALIBRATION) return nvs_cal_write(key, data, data_size);
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
    if (domain == STORAGE_DOMAIN_CALIBRATION) return nvs_cal_delete(key);
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
    if (domain == STORAGE_DOMAIN_CALIBRATION) return (nvs_cal_size(key) > 0) ? 1 : 0;
    if (!s_mounted || !key) return -1;

    char path[296];
    make_path(path, sizeof(path), key);

    struct stat st;
    return (stat(path, &st) == 0) ? 1 : 0;
}

int32_t Storage_GetSize(StorageDomain_t domain, const char *key)
{
    if (domain == STORAGE_DOMAIN_CALIBRATION) return nvs_cal_size(key);
    if (!s_mounted || !key) return -1;

    char path[296];
    make_path(path, sizeof(path), key);

    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (int32_t)st.st_size;
}
