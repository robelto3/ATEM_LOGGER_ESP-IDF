#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ltc.h"

#ifdef __cplusplus
extern "C" {
#endif

// Aplikační jádro střihových eventů.
// EDL výstup jde přes komponentu edl_writer.
//
// Logika:
// - první známý Program z ATEMu může založit výchozí segment bez CUT eventu,
// - skutečná změna Programu uzavře předchozí úsek a vypíše EDL událost,
// - nový Program současně založí další úsek.
//
// Čas eventu se bere ze společného app_state snapshotu nebo z TC snapshotu,
// který byl uložen při vložení události do logger fronty.
// TC ve snapshotu už je aplikační TCx2: vstupní LTC 25 fps -> sudé frame 00..48.

esp_err_t cut_event_init(void);

// Zaznamená skutečný střih / změnu Program vstupu.
void cut_event_record(uint8_t new_program_input);

// Varianta pro zpracování události z fronty.
// old_program_input je hodnota Programu ve chvíli, kdy událost vznikla.
// Díky tomu zůstane diagnostický výpis správný i tehdy, když app_state
// mezitím už ukazuje nový Program vstup.
void cut_event_record_with_previous(uint8_t old_program_input, uint8_t new_program_input);

// Stejné jako cut_event_record_with_previous(), ale čas střihu nepřebírá
// až při pomalém zpracování logger taskem. Použije TC snapshot uložený
// ve chvíli vložení události do fronty.
void cut_event_record_with_previous_at_tc(uint8_t old_program_input,
                                          uint8_t new_program_input,
                                          const ltc_time_t *cut_tc,
                                          bool cut_tc_valid);

// Synchronizuje aktuální Program z ATEMu bez vytvoření CUT eventu.
// Používá se pro první načtený stav po připojení/reconnectu.
// Pokud neběží žádný segment a TC je validní, založí výchozí segment v RAM.
// Pokud už segment běží, stejnou kameru ponechá; jinou kameru resynchronizuje bez EDL zápisu.
void cut_event_sync_program_start(uint8_t program_input);

// Varianta pro logger frontu: výchozí segment založí podle TC snapshotu
// uloženého ve chvíli vzniku SYNC události.
void cut_event_sync_program_start_at_tc(uint8_t program_input,
                                        const ltc_time_t *start_tc,
                                        bool start_tc_valid);

// cut_event_reset_session() zahodí případný rozpracovaný segment v RAM
// a vynuluje číslování pro novou EDL session.
void cut_event_reset_session(void);

// Uzavře právě rozpracovaný segment aktuálním TC ze společného app_state.
// Používá se při ukončení aktuální EDL session tlačítkem NEW FILE.
// Pokud žádný segment neběží, jen vypíše diagnostiku a vrátí false.
bool cut_event_close_active_segment_from_state(void);

// Varianta pro NEW FILE událost z fronty. Rozpracovaný segment uzavře
// TC snapshotem z okamžiku stisku tlačítka / vzniku požadavku.
bool cut_event_close_active_segment_at_tc(const ltc_time_t *out_tc, bool out_tc_valid);

uint32_t cut_event_get_cut_count(void);
uint32_t cut_event_get_edl_event_count(void);

// Zpětná kompatibilita se starším názvem.
uint32_t cut_event_get_count(void);

#ifdef __cplusplus
}
#endif
