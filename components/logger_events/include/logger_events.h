#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Fronta logger událostí.
//
// Smysl:
// - ATEM task ani tlačítka už přímo nezapisují EDL,
// - pouze vloží událost do fronty,
// - samostatný logger task ji zpracuje v pořadí.
//
// Je to příprava na budoucí dvoujádrové rozdělení:
// časově citlivé věci mohou běžet odděleně od pomalejších částí jako SD/web/display.

#define LOGGER_EVENTS_QUEUE_LENGTH 32

esp_err_t logger_events_init(void);

// První známý Program po připojení ATEMu.
// Neudělá CUT, jen založí/zesynchronizuje aktivní segment v RAM.
esp_err_t logger_events_submit_program_sync(uint8_t program_input);

// Skutečná změna Program busu.
// old_program_input je uložen kvůli korektní diagnostice CUT řádku.
esp_err_t logger_events_submit_program_cut(uint8_t old_program_input, uint8_t new_program_input);

// Ukončení aktuální EDL session a vytvoření nového souboru.
// Událost jde do stejné fronty, takže se provede až po předchozích CUT eventech.
esp_err_t logger_events_submit_new_file(void);

uint32_t logger_events_get_dropped_count(void);
uint32_t logger_events_get_waiting_count(void);

#ifdef __cplusplus
}
#endif
