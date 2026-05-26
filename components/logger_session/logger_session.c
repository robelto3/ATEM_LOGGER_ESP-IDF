#include "logger_session.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "app_state.h"
#include "debug_control.h"
#include "edl_writer.h"
#include "file_protect.h"
#include "sd_storage.h"
#include "show_config.h"

#define LOGGER_SESSION_DEFAULT_INDEX 1
#define LOGGER_SESSION_MAX_INDEX     99

#define LOGGER_SESSION_EDL_SUFFIX ".edl"
// Fyzický název souboru je DDMMRRNN.edl, například 04052601.edl.
// Mezery v číslování souborů se nevyplňují: nový soubor bere vždy nejvyšší
// nalezené číslo pro daný den + 1.
//
// EDL TITLE je lidsky čitelný název pořadu bez číslování:
// TITLE: Ranní vysílání
// Pořadové číslo je už ve fyzickém názvu souboru DDMMRRNN.edl.

typedef struct {
    bool valid;
    char title[LOGGER_SESSION_TITLE_MAX_LEN];
    char filename[LOGGER_SESSION_FILENAME_MAX_LEN];
    rtc_datetime_t created_rtc;
} logger_session_state_t;

static logger_session_state_t s_session;

static void logger_session_clear(void)
{
    memset(&s_session, 0, sizeof(s_session));
    strncpy(s_session.title, "NO_RTC", sizeof(s_session.title) - 1);
    strncpy(s_session.filename, "NO_RTC.edl", sizeof(s_session.filename) - 1);
}

static bool logger_session_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int logger_session_char_to_lower(int c)
{
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

static bool logger_session_parse_index_for_prefix(const char *filename,
                                                  const char *date_prefix,
                                                  uint8_t *index_out)
{
    if (!filename || !date_prefix || !index_out) {
        return false;
    }

    // DDMMRRNN.edl = 12 znaků
    if (strlen(filename) != 12) {
        return false;
    }

    if (strncmp(filename, date_prefix, 6) != 0) {
        return false;
    }

    if (!logger_session_is_digit(filename[6]) || !logger_session_is_digit(filename[7])) {
        return false;
    }

    if (strcmp(&filename[8], LOGGER_SESSION_EDL_SUFFIX) != 0) {
        return false;
    }

    uint8_t index = (uint8_t)((filename[6] - '0') * 10 + (filename[7] - '0'));
    if (index == 0 || index > LOGGER_SESSION_MAX_INDEX) {
        return false;
    }

    *index_out = index;
    return true;
}

static uint8_t logger_session_find_next_file_index(const rtc_datetime_t *rtc)
{
    if (!rtc || !sd_storage_is_mounted()) {
        return LOGGER_SESSION_DEFAULT_INDEX;
    }

    char date_prefix[16];
    snprintf(
        date_prefix,
        sizeof(date_prefix),
        "%02u%02u%02u",
        rtc->date,
        rtc->month,
        (unsigned)(rtc->year % 100U)
    );

    DIR *dir = opendir(SD_STORAGE_MOUNT_POINT);
    if (!dir) {
        if (debug_control_is_enabled()) {
            printf("LOGGER SESSION: cannot open %s, using index 01\n", SD_STORAGE_MOUNT_POINT);
        }
        return LOGGER_SESSION_DEFAULT_INDEX;
    }

    uint8_t max_index = 0;
    struct dirent *entry = NULL;

    while ((entry = readdir(dir)) != NULL) {
        uint8_t index = 0;
        if (logger_session_parse_index_for_prefix(entry->d_name, date_prefix, &index)) {
            if (index > max_index) {
                max_index = index;
            }
        }
    }

    closedir(dir);

    if (max_index >= LOGGER_SESSION_MAX_INDEX) {
        if (debug_control_is_enabled()) {
            printf("LOGGER SESSION: file index limit reached for %s, using 99\n", date_prefix);
        }
        return LOGGER_SESSION_MAX_INDEX;
    }

    return (uint8_t)(max_index + 1U);
}

static void logger_session_make_file_name(const rtc_datetime_t *rtc, uint8_t file_index)
{
    unsigned year_2digits = (unsigned)(rtc->year % 100U);

    snprintf(
        s_session.filename,
        sizeof(s_session.filename),
        "%02u%02u%02u%02u.edl",
        rtc->date,
        rtc->month,
        year_2digits,
        file_index
    );
}

static void logger_session_make_title(void)
{
    char show_name[SHOW_CONFIG_NAME_MAX_LEN];
    show_config_get_active_name(show_name, sizeof(show_name));

    snprintf(
        s_session.title,
        sizeof(s_session.title),
        "%s",
        show_name
    );
}

esp_err_t logger_session_start_new_from_rtc(const rtc_datetime_t *rtc, bool rtc_valid)
{
    logger_session_clear();

    if (rtc && rtc_valid) {
        uint8_t file_index = logger_session_find_next_file_index(rtc);

        s_session.valid = true;
        s_session.created_rtc = *rtc;
        logger_session_make_file_name(rtc, file_index);
        logger_session_make_title();
    }

    app_state_set_current_filename(s_session.filename);

    edl_writer_write_header(
        s_session.filename,
        s_session.title,
        &s_session.created_rtc,
        s_session.valid
    );

    esp_err_t protect_ret = file_protect_set_protected(s_session.filename, true);
    if (protect_ret != ESP_OK && debug_control_is_enabled()) {
        printf(
            "LOGGER SESSION: auto protect failed for %s, err=%s\n",
            s_session.filename,
            esp_err_to_name(protect_ret)
        );
    }

    return ESP_OK;
}

esp_err_t logger_session_init_from_rtc(const rtc_datetime_t *rtc, bool rtc_valid)
{
    return logger_session_start_new_from_rtc(rtc, rtc_valid);
}

const char *logger_session_get_title(void)
{
    return s_session.title;
}

const char *logger_session_get_filename(void)
{
    return s_session.filename;
}

bool logger_session_is_valid(void)
{
    return s_session.valid;
}
