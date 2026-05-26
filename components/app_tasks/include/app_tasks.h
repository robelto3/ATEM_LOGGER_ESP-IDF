#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Aplikační tasky pro rozdělení práce mezi dvě jádra.
// Rychlá část drží LTC snapshot v app_state a obsluhuje tlačítka.
// Pomalejší část obnovuje RTC/OLED a dělá periodické debug výpisy.

esp_err_t app_tasks_start(void);

#ifdef __cplusplus
}
#endif
