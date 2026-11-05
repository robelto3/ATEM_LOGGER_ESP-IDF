#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "rtc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOGGER_SESSION_TITLE_MAX_LEN     96
#define LOGGER_SESSION_FILENAME_MAX_LEN  32

typedef enum {
    LOGGER_SESSION_IDLE = 0,
    LOGGER_SESSION_STARTING,
    LOGGER_SESSION_ACTIVE,
    LOGGER_SESSION_STOPPING,
    LOGGER_SESSION_ERROR,
} logger_session_status_t;

typedef struct {
    logger_session_status_t status;
    bool valid;
    char title[LOGGER_SESSION_TITLE_MAX_LEN];
    char filename[LOGGER_SESSION_FILENAME_MAX_LEN];
    char last_closed_filename[LOGGER_SESSION_FILENAME_MAX_LEN];
    rtc_datetime_t created_rtc;
    esp_err_t last_error;
    uint32_t generation;
} logger_session_snapshot_t;

// Logger session:
// - vytvoří fyzický název EDL souboru podle RTC: DDMMRRNN.edl,
// - pořadové číslo NN v názvu souboru hledá na SD kartě jako nejvyšší existující pro daný den + 1,
// - mezery v číslování souborů nevyplňuje,
// - EDL TITLE vytváří jako aktivní název pořadu bez číslování, např. Ranní vysílání,
// - pořadové číslo zůstává jen ve fyzickém názvu souboru DDMMRRNN.edl,
// - uloží filename do app_state pro display,
// - předá EDL hlavičku komponentě edl_writer.

// Po bootu zůstává logger neaktivní. Nový EDL založí až explicitní START.
esp_err_t logger_session_init_idle(void);

// Ponecháno pro servisní/manual NEW FILE cestu.
esp_err_t logger_session_init_from_rtc(const rtc_datetime_t *rtc, bool rtc_valid);
esp_err_t logger_session_start_new_from_rtc(const rtc_datetime_t *rtc, bool rtc_valid);
esp_err_t logger_session_stop(void);

const char *logger_session_get_title(void);
const char *logger_session_get_filename(void);
bool logger_session_is_valid(void);
bool logger_session_is_active(void);
const char *logger_session_status_name(logger_session_status_t status);
void logger_session_get_snapshot(logger_session_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
