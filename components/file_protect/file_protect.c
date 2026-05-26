#include "file_protect.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "nvs.h"

#define FILE_PROTECT_NVS_NAMESPACE "fileprot"
#define FILE_PROTECT_KEY_LEN       16
#define FILE_PROTECT_NAME_MAX_LEN  64

static bool file_protect_filename_is_safe(const char *filename)
{
    if (!filename || filename[0] == '\0') {
        return false;
    }

    size_t len = strlen(filename);
    if (len >= FILE_PROTECT_NAME_MAX_LEN) {
        return false;
    }

    if (strstr(filename, "..") != NULL) {
        return false;
    }

    for (const char *p = filename; *p; p++) {
        char c = *p;
        bool ok =
            (c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            c == '.' || c == '_' || c == '-';

        if (!ok) {
            return false;
        }
    }

    return true;
}

static uint32_t file_protect_hash_filename(const char *filename)
{
    // FNV-1a 32bit. NVS key pak zůstane krátký i pro případ delšího názvu.
    uint32_t hash = 2166136261UL;
    for (const unsigned char *p = (const unsigned char *)filename; *p; p++) {
        hash ^= (uint32_t)(*p);
        hash *= 16777619UL;
    }
    return hash;
}

static bool file_protect_make_key(const char *filename, char *key, size_t key_len)
{
    if (!file_protect_filename_is_safe(filename) || !key || key_len < FILE_PROTECT_KEY_LEN) {
        return false;
    }

    uint32_t hash = file_protect_hash_filename(filename);
    int written = snprintf(key, key_len, "p%08lx", (unsigned long)hash);
    return (written > 0 && written < (int)key_len);
}

esp_err_t file_protect_init(void)
{
    // NVS inicializuje net_config při startu aplikace.
    // Tady si jen ověříme/otevřeme namespace, aby případná chyba byla vidět při initu.
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(FILE_PROTECT_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        nvs_close(handle);
    }
    return ret;
}

bool file_protect_is_protected(const char *filename)
{
    char key[FILE_PROTECT_KEY_LEN] = {0};
    if (!file_protect_make_key(filename, key, sizeof(key))) {
        return false;
    }

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(FILE_PROTECT_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return false;
    }

    char stored_name[FILE_PROTECT_NAME_MAX_LEN] = {0};
    size_t len = sizeof(stored_name);
    ret = nvs_get_str(handle, key, stored_name, &len);
    nvs_close(handle);

    return (ret == ESP_OK && strcmp(stored_name, filename) == 0);
}

esp_err_t file_protect_set_protected(const char *filename, bool protected_file)
{
    char key[FILE_PROTECT_KEY_LEN] = {0};
    if (!file_protect_make_key(filename, key, sizeof(key))) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(FILE_PROTECT_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    if (protected_file) {
        ret = nvs_set_str(handle, key, filename);
    } else {
        ret = nvs_erase_key(handle, key);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ret = ESP_OK;
        }
    }

    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    return ret;
}
