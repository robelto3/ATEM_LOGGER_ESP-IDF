#include "show_config.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "nvs.h"

#define SHOW_CONFIG_NVS_NAMESPACE "showcfg"
#define SHOW_CONFIG_NVS_KEY_ACTIVE "active"

static portMUX_TYPE s_show_config_mux = portMUX_INITIALIZER_UNLOCKED;
static char s_show_names[SHOW_CONFIG_SLOT_COUNT][SHOW_CONFIG_NAME_MAX_LEN];
static uint8_t s_active_index = 0;
static bool s_initialized = false;

static void show_config_make_name_key(uint8_t index, char *out, size_t out_size)
{
    if (!out || out_size == 0U) {
        return;
    }
    snprintf(out, out_size, "name%u", (unsigned)(index + 1U));
}

static bool show_config_name_is_empty(const char *name)
{
    return (!name || name[0] == '\0');
}

void show_config_sanitize_name(const char *in, char *out, size_t out_size)
{
    if (!out || out_size == 0U) {
        return;
    }

    out[0] = '\0';
    if (!in) {
        return;
    }

    char tmp[SHOW_CONFIG_NAME_MAX_LEN];
    size_t pos = 0;
    bool last_was_space = true;

    for (const unsigned char *p = (const unsigned char *)in; *p && pos + 1U < sizeof(tmp); p++) {
        unsigned char c = *p;

        if (c == '\r' || c == '\n' || c == '\t') {
            c = ' ';
        }

        // ASCII řídicí znaky vynecháme. Bajty >= 0x80 ponecháme kvůli UTF-8 diakritice.
        if (c < 32U) {
            continue;
        }

        if (c == ' ') {
            if (last_was_space) {
                continue;
            }
            last_was_space = true;
        } else {
            last_was_space = false;
        }

        tmp[pos++] = (char)c;
    }

    while (pos > 0U && tmp[pos - 1U] == ' ') {
        pos--;
    }

    tmp[pos] = '\0';
    snprintf(out, out_size, "%s", tmp);
}

esp_err_t show_config_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    char loaded_names[SHOW_CONFIG_SLOT_COUNT][SHOW_CONFIG_NAME_MAX_LEN];
    memset(loaded_names, 0, sizeof(loaded_names));
    snprintf(loaded_names[0], sizeof(loaded_names[0]), "%s", SHOW_CONFIG_DEFAULT_NAME);
    uint8_t loaded_active = 0;

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SHOW_CONFIG_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_OK) {
        uint8_t active = 0;
        if (nvs_get_u8(handle, SHOW_CONFIG_NVS_KEY_ACTIVE, &active) == ESP_OK && active < SHOW_CONFIG_SLOT_COUNT) {
            loaded_active = active;
        }

        for (uint8_t i = 0; i < SHOW_CONFIG_SLOT_COUNT; i++) {
            char key[12];
            char raw[SHOW_CONFIG_NAME_MAX_LEN] = {0};
            size_t len = sizeof(raw);
            show_config_make_name_key(i, key, sizeof(key));

            if (nvs_get_str(handle, key, raw, &len) == ESP_OK) {
                show_config_sanitize_name(raw, loaded_names[i], sizeof(loaded_names[i]));
            }
        }

        nvs_close(handle);
    } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
        return ret;
    }

    if (show_config_name_is_empty(loaded_names[0])) {
        snprintf(loaded_names[0], sizeof(loaded_names[0]), "%s", SHOW_CONFIG_DEFAULT_NAME);
    }

    portENTER_CRITICAL(&s_show_config_mux);
    memcpy(s_show_names, loaded_names, sizeof(s_show_names));
    s_active_index = loaded_active;
    s_initialized = true;
    portEXIT_CRITICAL(&s_show_config_mux);

    return ESP_OK;
}

void show_config_get_name(uint8_t index, char *out, size_t out_size)
{
    if (!out || out_size == 0U) {
        return;
    }

    out[0] = '\0';
    if (index >= SHOW_CONFIG_SLOT_COUNT) {
        return;
    }

    portENTER_CRITICAL(&s_show_config_mux);
    snprintf(out, out_size, "%s", s_show_names[index]);
    portEXIT_CRITICAL(&s_show_config_mux);
}

esp_err_t show_config_set_name(uint8_t index, const char *name)
{
    if (index >= SHOW_CONFIG_SLOT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    char clean[SHOW_CONFIG_NAME_MAX_LEN];
    show_config_sanitize_name(name, clean, sizeof(clean));

    // Slot 1 nesmí být prázdný, aby vždy existoval bezpečný fallback.
    if (index == 0U && show_config_name_is_empty(clean)) {
        snprintf(clean, sizeof(clean), "%s", SHOW_CONFIG_DEFAULT_NAME);
    }

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SHOW_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    char key[12];
    show_config_make_name_key(index, key, sizeof(key));
    ret = nvs_set_str(handle, key, clean);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);

    if (ret != ESP_OK) {
        return ret;
    }

    portENTER_CRITICAL(&s_show_config_mux);
    snprintf(s_show_names[index], sizeof(s_show_names[index]), "%s", clean);
    portEXIT_CRITICAL(&s_show_config_mux);

    return ESP_OK;
}

uint8_t show_config_get_active_index(void)
{
    uint8_t active = 0;
    portENTER_CRITICAL(&s_show_config_mux);
    active = s_active_index;
    portEXIT_CRITICAL(&s_show_config_mux);
    return active;
}

esp_err_t show_config_set_active_index(uint8_t index)
{
    if (index >= SHOW_CONFIG_SLOT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(SHOW_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_u8(handle, SHOW_CONFIG_NVS_KEY_ACTIVE, index);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);

    if (ret != ESP_OK) {
        return ret;
    }

    portENTER_CRITICAL(&s_show_config_mux);
    s_active_index = index;
    portEXIT_CRITICAL(&s_show_config_mux);

    return ESP_OK;
}

void show_config_get_active_name(char *out, size_t out_size)
{
    if (!out || out_size == 0U) {
        return;
    }

    char name[SHOW_CONFIG_NAME_MAX_LEN] = {0};

    portENTER_CRITICAL(&s_show_config_mux);
    uint8_t active = s_active_index;
    if (active >= SHOW_CONFIG_SLOT_COUNT) {
        active = 0;
    }
    snprintf(name, sizeof(name), "%s", s_show_names[active]);
    if (show_config_name_is_empty(name)) {
        snprintf(name, sizeof(name), "%s", s_show_names[0]);
    }
    portEXIT_CRITICAL(&s_show_config_mux);

    if (show_config_name_is_empty(name)) {
        snprintf(name, sizeof(name), "%s", SHOW_CONFIG_DEFAULT_NAME);
    }

    snprintf(out, out_size, "%s", name);
}
