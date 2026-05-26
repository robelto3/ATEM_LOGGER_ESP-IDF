#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Ochrana souborů proti nechtěnému smazání.
// Stav se ukládá do NVS flash ESP32-P4. V uživatelském webu se to chová jako
// jednoduchá EEPROM paměť, ale technicky jde o ESP-IDF NVS.

esp_err_t file_protect_init(void);

bool file_protect_is_protected(const char *filename);
esp_err_t file_protect_set_protected(const char *filename, bool protected_file);

#ifdef __cplusplus
}
#endif
