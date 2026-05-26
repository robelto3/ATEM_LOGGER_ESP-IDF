#include "net_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NET_CONFIG_NVS_NAMESPACE "netcfg"
#define NET_CONFIG_NVS_KEY_SERVER_IP "server_ip"
#define NET_CONFIG_NVS_KEY_ATEM_IP   "atem_ip"
#define NET_CONFIG_NVS_KEY_PREVIEW_TALLY "preview_tally"

static const char *TAG = "NET_CONFIG";

static portMUX_TYPE s_config_mux = portMUX_INITIALIZER_UNLOCKED;
static net_config_ip4_t s_server_ip = {10, 0, 0, 9};
static net_config_ip4_t s_atem_ip = {10, 0, 0, 10};
static bool s_preview_tally_enabled = true;
static bool s_initialized = false;

static bool net_config_is_usable_host_ip(const net_config_ip4_t *ip)
{
    if (!ip) {
        return false;
    }

    // Základní ochrana proti nesmyslným adresám.
    // 0.x.x.x a multicast/reserved rozsah nechceme.
    if (ip->a == 0U || ip->a >= 224U) {
        return false;
    }

    // Adresa s host částí .0 nebo .255 bývá v /24 síti síťová/broadcast.
    if (ip->d == 0U || ip->d == 255U) {
        return false;
    }

    return true;
}

bool net_config_parse_ip4(const char *text, net_config_ip4_t *ip)
{
    if (!text || !ip) {
        return false;
    }

    unsigned a = 0;
    unsigned b = 0;
    unsigned c = 0;
    unsigned d = 0;
    char tail = '\0';

    int parsed = sscanf(text, " %u.%u.%u.%u %c", &a, &b, &c, &d, &tail);
    if (parsed != 4) {
        return false;
    }

    if (a > 255U || b > 255U || c > 255U || d > 255U) {
        return false;
    }

    net_config_ip4_t tmp = {
        .a = (uint8_t)a,
        .b = (uint8_t)b,
        .c = (uint8_t)c,
        .d = (uint8_t)d,
    };

    if (!net_config_is_usable_host_ip(&tmp)) {
        return false;
    }

    *ip = tmp;
    return true;
}

void net_config_ip4_to_string(const net_config_ip4_t *ip, char *out, size_t out_size)
{
    if (!out || out_size == 0U) {
        return;
    }

    if (!ip) {
        snprintf(out, out_size, "0.0.0.0");
        return;
    }

    snprintf(out, out_size, "%u.%u.%u.%u", ip->a, ip->b, ip->c, ip->d);
}

static esp_err_t net_config_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase: %s", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    return ret;
}

static void net_config_set_default_server_ip_locked(void)
{
    s_server_ip.a = 10;
    s_server_ip.b = 0;
    s_server_ip.c = 0;
    s_server_ip.d = 9;
}

static void net_config_set_default_atem_ip_locked(void)
{
    s_atem_ip.a = 10;
    s_atem_ip.b = 0;
    s_atem_ip.c = 0;
    s_atem_ip.d = 10;
}

static bool net_config_read_ip_from_nvs(nvs_handle_t handle, const char *key, net_config_ip4_t *ip)
{
    if (!key || !ip) {
        return false;
    }

    char ip_text[NET_CONFIG_IP_STR_LEN] = {0};
    size_t len = sizeof(ip_text);
    esp_err_t ret = nvs_get_str(handle, key, ip_text, &len);
    if (ret == ESP_OK) {
        bool ok = net_config_parse_ip4(ip_text, ip);
        if (!ok) {
            ESP_LOGW(TAG, "Stored IP for key '%s' is invalid: '%s'", key, ip_text);
        }
        return ok;
    }

    if (ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Read IP key '%s' failed: %s", key, esp_err_to_name(ret));
    }

    return false;
}

static bool net_config_read_bool_from_nvs(nvs_handle_t handle, const char *key, bool *value)
{
    if (!key || !value) {
        return false;
    }

    uint8_t stored = 0;
    esp_err_t ret = nvs_get_u8(handle, key, &stored);
    if (ret == ESP_OK) {
        *value = (stored != 0U);
        return true;
    }

    if (ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Read bool key '%s' failed: %s", key, esp_err_to_name(ret));
    }

    return false;
}

esp_err_t net_config_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = net_config_nvs_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    net_config_ip4_t loaded_server_ip = {0};
    net_config_ip4_t loaded_atem_ip = {0};
    bool loaded_preview_tally_enabled = true;
    bool loaded_server_ok = false;
    bool loaded_atem_ok = false;
    bool loaded_preview_tally_ok = false;

    nvs_handle_t handle = 0;
    ret = nvs_open(NET_CONFIG_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_OK) {
        loaded_server_ok = net_config_read_ip_from_nvs(handle, NET_CONFIG_NVS_KEY_SERVER_IP, &loaded_server_ip);
        loaded_atem_ok = net_config_read_ip_from_nvs(handle, NET_CONFIG_NVS_KEY_ATEM_IP, &loaded_atem_ip);
        loaded_preview_tally_ok = net_config_read_bool_from_nvs(handle, NET_CONFIG_NVS_KEY_PREVIEW_TALLY, &loaded_preview_tally_enabled);
        nvs_close(handle);
    } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Open NVS namespace failed: %s", esp_err_to_name(ret));
    }

    portENTER_CRITICAL(&s_config_mux);
    if (loaded_server_ok) {
        s_server_ip = loaded_server_ip;
    } else {
        net_config_set_default_server_ip_locked();
    }

    if (loaded_atem_ok) {
        s_atem_ip = loaded_atem_ip;
    } else {
        net_config_set_default_atem_ip_locked();
    }

    s_preview_tally_enabled = loaded_preview_tally_ok ? loaded_preview_tally_enabled : true;

    s_initialized = true;
    portEXIT_CRITICAL(&s_config_mux);

    char server_ip_str[NET_CONFIG_IP_STR_LEN] = {0};
    char atem_ip_str[NET_CONFIG_IP_STR_LEN] = {0};
    net_config_get_server_ip_string(server_ip_str, sizeof(server_ip_str));
    net_config_get_atem_ip_string(atem_ip_str, sizeof(atem_ip_str));

    ESP_LOGI(TAG, "Server IP: %s%s", server_ip_str, loaded_server_ok ? " (NVS)" : " (default)");
    ESP_LOGI(TAG, "ATEM IP:   %s%s", atem_ip_str, loaded_atem_ok ? " (NVS)" : " (default)");
    ESP_LOGI(TAG, "Preview tally: %s%s", s_preview_tally_enabled ? "ON" : "OFF", loaded_preview_tally_ok ? " (NVS)" : " (default)");

    return ESP_OK;
}

void net_config_get_server_ip(net_config_ip4_t *ip)
{
    if (!ip) {
        return;
    }

    portENTER_CRITICAL(&s_config_mux);
    *ip = s_server_ip;
    portEXIT_CRITICAL(&s_config_mux);
}

void net_config_get_atem_ip(net_config_ip4_t *ip)
{
    if (!ip) {
        return;
    }

    portENTER_CRITICAL(&s_config_mux);
    *ip = s_atem_ip;
    portEXIT_CRITICAL(&s_config_mux);
}

void net_config_get_netmask(net_config_ip4_t *ip)
{
    if (!ip) {
        return;
    }

    *ip = (net_config_ip4_t){255, 255, 255, 0};
}

void net_config_get_gateway(net_config_ip4_t *ip)
{
    if (!ip) {
        return;
    }

    net_config_ip4_t server = {0};
    net_config_get_server_ip(&server);

    // Jednoduché pravidlo pro běžnou /24 síť:
    // gateway = stejná síť, poslední oktet 1.
    *ip = (net_config_ip4_t){server.a, server.b, server.c, 1};
}

void net_config_get_server_ip_string(char *out, size_t out_size)
{
    net_config_ip4_t ip = {0};
    net_config_get_server_ip(&ip);
    net_config_ip4_to_string(&ip, out, out_size);
}

void net_config_get_atem_ip_string(char *out, size_t out_size)
{
    net_config_ip4_t ip = {0};
    net_config_get_atem_ip(&ip);
    net_config_ip4_to_string(&ip, out, out_size);
}

void net_config_get_netmask_string(char *out, size_t out_size)
{
    net_config_ip4_t ip = {0};
    net_config_get_netmask(&ip);
    net_config_ip4_to_string(&ip, out, out_size);
}

void net_config_get_gateway_string(char *out, size_t out_size)
{
    net_config_ip4_t ip = {0};
    net_config_get_gateway(&ip);
    net_config_ip4_to_string(&ip, out, out_size);
}

static esp_err_t net_config_set_ip_string(const char *key, const char *ip_text, net_config_ip4_t *target)
{
    if (!key || !target) {
        return ESP_ERR_INVALID_ARG;
    }

    net_config_ip4_t ip = {0};
    if (!net_config_parse_ip4(ip_text, &ip)) {
        return ESP_ERR_INVALID_ARG;
    }

    char normalized[NET_CONFIG_IP_STR_LEN] = {0};
    net_config_ip4_to_string(&ip, normalized, sizeof(normalized));

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(NET_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_str(handle, key, normalized);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);

    if (ret != ESP_OK) {
        return ret;
    }

    portENTER_CRITICAL(&s_config_mux);
    *target = ip;
    portEXIT_CRITICAL(&s_config_mux);

    ESP_LOGI(TAG, "IP saved for key '%s': %s", key, normalized);
    return ESP_OK;
}

esp_err_t net_config_set_server_ip_string(const char *ip_text)
{
    return net_config_set_ip_string(NET_CONFIG_NVS_KEY_SERVER_IP, ip_text, &s_server_ip);
}

esp_err_t net_config_set_atem_ip_string(const char *ip_text)
{
    return net_config_set_ip_string(NET_CONFIG_NVS_KEY_ATEM_IP, ip_text, &s_atem_ip);
}


bool net_config_get_preview_tally_enabled(void)
{
    bool enabled = true;

    portENTER_CRITICAL(&s_config_mux);
    enabled = s_preview_tally_enabled;
    portEXIT_CRITICAL(&s_config_mux);

    return enabled;
}

esp_err_t net_config_set_preview_tally_enabled(bool enabled)
{
    portENTER_CRITICAL(&s_config_mux);
    bool old_enabled = s_preview_tally_enabled;
    portEXIT_CRITICAL(&s_config_mux);

    if (old_enabled == enabled) {
        return ESP_OK;
    }

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(NET_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_u8(handle, NET_CONFIG_NVS_KEY_PREVIEW_TALLY, enabled ? 1U : 0U);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);

    if (ret != ESP_OK) {
        return ret;
    }

    portENTER_CRITICAL(&s_config_mux);
    s_preview_tally_enabled = enabled;
    portEXIT_CRITICAL(&s_config_mux);

    ESP_LOGI(TAG, "Preview tally saved: %s", enabled ? "ON" : "OFF");
    return ESP_OK;
}
