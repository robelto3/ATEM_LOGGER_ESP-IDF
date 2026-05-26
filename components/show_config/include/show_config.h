#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHOW_CONFIG_SLOT_COUNT      5
#define SHOW_CONFIG_NAME_MAX_LEN    64
#define SHOW_CONFIG_TITLE_MAX_LEN   96
#define SHOW_CONFIG_DEFAULT_NAME    "ATEM LOGGER"

// Trvalé nastavení názvů pořadů.
// Do NVS se ukládají pouze názvy a aktivní výběr.
// EDL TITLE používá aktivní název pořadu bez číslování.

esp_err_t show_config_init(void);

void show_config_get_name(uint8_t index, char *out, size_t out_size);
esp_err_t show_config_set_name(uint8_t index, const char *name);

uint8_t show_config_get_active_index(void);
esp_err_t show_config_set_active_index(uint8_t index);
void show_config_get_active_name(char *out, size_t out_size);

// Sanitizace textu z webu/UART:
// - odstraní CR/LF a řídicí znaky,
// - ořízne mezery na začátku/konci,
// - ponechá UTF-8 bajty pro českou diakritiku.
void show_config_sanitize_name(const char *in, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
