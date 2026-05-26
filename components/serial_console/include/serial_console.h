#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Jednoduchá UART konzole pro servisní nastavení.
// Umí změnu IP adresy ESP/web serveru a runtime zapnutí/vypnutí debug výpisů.

esp_err_t serial_console_init(void);

#ifdef __cplusplus
}
#endif
