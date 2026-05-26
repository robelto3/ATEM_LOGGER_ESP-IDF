#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Fake střih pro testování bez ATEMu.
// Zapojení: tlačítko mezi GPIO46 a GND.
// Vstup je aktivní v log. 0 a používá interní pull-up.
#define FAKE_CUT_BUTTON_GPIO 46

esp_err_t fake_cut_button_init(void);
bool fake_cut_button_was_pressed(void);

#ifdef __cplusplus
}
#endif
