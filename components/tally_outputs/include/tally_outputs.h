#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Inicializuje GPIO výstupy pro Program/Preview tally a zhasne je.
esp_err_t tally_outputs_init(void);

// Aktualizuje tally výstupy podle aktuálního app_state Program/Preview.
void tally_outputs_update(void);

// Vynutí zhasnutí všech tally výstupů.
void tally_outputs_all_off(void);

#ifdef __cplusplus
}
#endif
