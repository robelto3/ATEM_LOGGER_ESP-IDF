#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Trvalé síťové nastavení loggeru.
// ESP/server IP jde měnit přes UART i web.
// ATEM IP jde měnit pouze přes web.

#define NET_CONFIG_DEFAULT_SERVER_IP "10.0.0.9"
#define NET_CONFIG_DEFAULT_ATEM_IP   "10.0.0.10"
#define NET_CONFIG_DEFAULT_NETMASK   "255.255.255.0"

#define NET_CONFIG_IP_STR_LEN 16

typedef struct {
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint8_t d;
} net_config_ip4_t;

esp_err_t net_config_init(void);

void net_config_get_server_ip(net_config_ip4_t *ip);
void net_config_get_atem_ip(net_config_ip4_t *ip);
void net_config_get_netmask(net_config_ip4_t *ip);

void net_config_get_server_ip_string(char *out, size_t out_size);
void net_config_get_atem_ip_string(char *out, size_t out_size);
void net_config_get_netmask_string(char *out, size_t out_size);

bool net_config_parse_ip4(const char *text, net_config_ip4_t *ip);
void net_config_ip4_to_string(const net_config_ip4_t *ip, char *out, size_t out_size);

esp_err_t net_config_set_server_ip_string(const char *ip_text);
esp_err_t net_config_set_atem_ip_string(const char *ip_text);

// Nastavení Program/Preview tally výstupů.
// true  = dané tally výstupy jsou aktivní
// false = dané tally výstupy jsou vypnuté a drží se zhasnuté
bool net_config_get_program_tally_enabled(void);
esp_err_t net_config_set_program_tally_enabled(bool enabled);
bool net_config_get_preview_tally_enabled(void);
esp_err_t net_config_set_preview_tally_enabled(bool enabled);

// Korekce vstupního LTC v původních 25fps framech.
// Záporná hodnota posune logovaný TC zpět, kladná dopředu.
#define NET_CONFIG_LTC_CORRECTION_MIN -24
#define NET_CONFIG_LTC_CORRECTION_MAX 24
int net_config_get_ltc_frame_correction(void);
esp_err_t net_config_set_ltc_frame_correction(int correction_frames);

#ifdef __cplusplus
}
#endif
