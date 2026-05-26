#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t display_init(void);
esp_err_t display_clear(void);

esp_err_t display_show_startup_screen(const char *server_ip, const char *atem_ip);
esp_err_t display_show_main_screen(void);

#ifdef __cplusplus
}
#endif
