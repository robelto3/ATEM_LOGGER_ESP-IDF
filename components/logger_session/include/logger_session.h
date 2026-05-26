#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "rtc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOGGER_SESSION_TITLE_MAX_LEN     96
#define LOGGER_SESSION_FILENAME_MAX_LEN  32

// Logger session:
// - vytvoří fyzický název EDL souboru podle RTC: DDMMRRNN.edl,
// - pořadové číslo NN v názvu souboru hledá na SD kartě jako nejvyšší existující pro daný den + 1,
// - mezery v číslování souborů nevyplňuje,
// - EDL TITLE vytváří jako aktivní název pořadu bez číslování, např. Ranní vysílání,
// - pořadové číslo zůstává jen ve fyzickém názvu souboru DDMMRRNN.edl,
// - uloží filename do app_state pro display,
// - předá EDL hlavičku komponentě edl_writer.

esp_err_t logger_session_init_from_rtc(const rtc_datetime_t *rtc, bool rtc_valid);
esp_err_t logger_session_start_new_from_rtc(const rtc_datetime_t *rtc, bool rtc_valid);

const char *logger_session_get_title(void);
const char *logger_session_get_filename(void);
bool logger_session_is_valid(void);

#ifdef __cplusplus
}
#endif
