#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Tlačítko pro ukončení aktuální EDL session a vytvoření nového souboru.
// Zapojení: tlačítko mezi GPIO5 a GND.
// Vstup je aktivní v log. 0 a používá interní pull-up.
#define NEW_FILE_BUTTON_GPIO 5

esp_err_t new_file_button_init(void);
bool new_file_button_was_pressed(void);

#ifdef __cplusplus
}
#endif
