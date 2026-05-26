#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Jednoduchý web pro práci se soubory na SD kartě:
//   /         Home stránka loggeru + odkaz na aktuální EDL
//   /files    seznam souborů na /sdcard
//   /view     zobrazení souboru: /view?file=03052601.edl
//   /download stažení souboru:   /download?file=03052601.edl
//   /network  nastavení IP adres ESP/web serveru a ATEMu

esp_err_t web_server_start(void);
void web_server_stop(void);

#ifdef __cplusplus
}
#endif
