#include "logger_session.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "app_state.h"
#include "debug_control.h"
#include "edl_writer.h"
#include "file_protect.h"
#include "freertos/FreeRTOS.h"
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

static logger_session_snapshot_t s_session;
static portMUX_TYPE s_session_mux = portMUX_INITIALIZER_UNLOCKED;

static void logger_session_clear(void)
{
    portENTER_CRITICAL(&s_session_mux);
    memset(&s_session, 0, sizeof(s_session));
    s_session.status = LOGGER_SESSION_IDLE;
    s_session.last_error = ESP_OK;
    portEXIT_CRITICAL(&s_session_mux);
}

static bool logger_session_is_digit(char c)
{
    return c >= '0' && c <= '9';
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

static void logger_session_make_file_name(const rtc_datetime_t *rtc,
                                          uint8_t file_index,
                                          char *out,
                                          size_t out_len)
{
    unsigned year_2digits = (unsigned)(rtc->year % 100U);

    snprintf(
        out,
        out_len,
        "%02u%02u%02u%02u.edl",
        rtc->date,
        rtc->month,
        year_2digits,
        file_index
    );
}

static void logger_session_make_title(char *out, size_t out_len)
{
    char show_name[SHOW_CONFIG_NAME_MAX_LEN];
    show_config_get_active_name(show_name, sizeof(show_name));

    snprintf(out, out_len, "%s", show_name);
}

static void logger_session_set_error(esp_err_t error, const char *filename)
{
    portENTER_CRITICAL(&s_session_mux);
    s_session.status = LOGGER_SESSION_ERROR;
    s_session.valid = false;
    s_session.last_error = error;
    s_session.title[0] = '\0';
    memset(&s_session.created_rtc, 0, sizeof(s_session.created_rtc));
    if (filename) {
        snprintf(s_session.filename, sizeof(s_session.filename), "%s", filename);
    } else {
        s_session.filename[0] = '\0';
    }
    portEXIT_CRITICAL(&s_session_mux);

    app_state_set_current_filename("---");
}

esp_err_t logger_session_init_idle(void)
{
    logger_session_clear();
    (void)edl_writer_stop();
    app_state_set_current_filename("---");
    return ESP_OK;
}

esp_err_t logger_session_start_new_from_rtc(const rtc_datetime_t *rtc, bool rtc_valid)
{
    char filename[LOGGER_SESSION_FILENAME_MAX_LEN] = {0};
    char title[LOGGER_SESSION_TITLE_MAX_LEN] = {0};

    portENTER_CRITICAL(&s_session_mux);
    s_session.status = LOGGER_SESSION_STARTING;
    s_session.valid = false;
    s_session.last_error = ESP_OK;
    s_session.title[0] = '\0';
    s_session.filename[0] = '\0';
    memset(&s_session.created_rtc, 0, sizeof(s_session.created_rtc));
    portEXIT_CRITICAL(&s_session_mux);

    if (!rtc || !rtc_valid) {
        (void)edl_writer_stop();
        logger_session_set_error(ESP_ERR_INVALID_STATE, NULL);
        return ESP_ERR_INVALID_STATE;
    }

    if (!sd_storage_is_mounted()) {
        (void)edl_writer_stop();
        logger_session_set_error(ESP_ERR_INVALID_STATE, NULL);
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t file_index = logger_session_find_next_file_index(rtc);
    logger_session_make_file_name(rtc, file_index, filename, sizeof(filename));
    logger_session_make_title(title, sizeof(title));

    esp_err_t writer_ret = edl_writer_write_header(filename, title, rtc, true);
    if (writer_ret != ESP_OK) {
        logger_session_set_error(writer_ret, filename);
        return writer_ret;
    }

    esp_err_t protect_ret = file_protect_set_protected(filename, true);
    if (protect_ret != ESP_OK && debug_control_is_enabled()) {
        printf(
            "LOGGER SESSION: auto protect failed for %s, err=%s\n",
            filename,
            esp_err_to_name(protect_ret)
        );
    }

    portENTER_CRITICAL(&s_session_mux);
    s_session.status = LOGGER_SESSION_ACTIVE;
    s_session.valid = true;
    s_session.last_error = ESP_OK;
    s_session.created_rtc = *rtc;
    snprintf(s_session.filename, sizeof(s_session.filename), "%s", filename);
    snprintf(s_session.title, sizeof(s_session.title), "%s", title);
    s_session.generation++;
    portEXIT_CRITICAL(&s_session_mux);

    app_state_set_current_filename(filename);

    return ESP_OK;
}

esp_err_t logger_session_init_from_rtc(const rtc_datetime_t *rtc, bool rtc_valid)
{
    return logger_session_start_new_from_rtc(rtc, rtc_valid);
}

esp_err_t logger_session_stop(void)
{
    char closing_filename[LOGGER_SESSION_FILENAME_MAX_LEN] = {0};
    bool was_active = false;

    portENTER_CRITICAL(&s_session_mux);
    was_active = (s_session.status == LOGGER_SESSION_ACTIVE);
    if (was_active) {
        snprintf(closing_filename, sizeof(closing_filename), "%s", s_session.filename);
    }
    s_session.status = LOGGER_SESSION_STOPPING;
    portEXIT_CRITICAL(&s_session_mux);

    esp_err_t ret = edl_writer_stop();

    portENTER_CRITICAL(&s_session_mux);
    if (ret == ESP_OK) {
        if (was_active && closing_filename[0] != '\0') {
            snprintf(
                s_session.last_closed_filename,
                sizeof(s_session.last_closed_filename),
                "%s",
                closing_filename
            );
        }
        s_session.status = LOGGER_SESSION_IDLE;
        s_session.valid = false;
        s_session.last_error = ESP_OK;
        s_session.title[0] = '\0';
        s_session.filename[0] = '\0';
        memset(&s_session.created_rtc, 0, sizeof(s_session.created_rtc));
    } else {
        s_session.status = LOGGER_SESSION_ERROR;
        s_session.last_error = ret;
    }
    portEXIT_CRITICAL(&s_session_mux);

    if (ret == ESP_OK) {
        app_state_set_current_filename("---");
    }

    return ret;
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
    bool valid;
    portENTER_CRITICAL(&s_session_mux);
    valid = s_session.valid;
    portEXIT_CRITICAL(&s_session_mux);
    return valid;
}

bool logger_session_is_active(void)
{
    bool active;
    portENTER_CRITICAL(&s_session_mux);
    active = (s_session.status == LOGGER_SESSION_ACTIVE);
    portEXIT_CRITICAL(&s_session_mux);
    return active;
}

const char *logger_session_status_name(logger_session_status_t status)
{
    switch (status) {
    case LOGGER_SESSION_STARTING:
        return "starting";
    case LOGGER_SESSION_ACTIVE:
        return "active";
    case LOGGER_SESSION_STOPPING:
        return "stopping";
    case LOGGER_SESSION_ERROR:
        return "error";
    case LOGGER_SESSION_IDLE:
    default:
        return "idle";
    }
}

void logger_session_get_snapshot(logger_session_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }

    portENTER_CRITICAL(&s_session_mux);
    *snapshot = s_session;
    portEXIT_CRITICAL(&s_session_mux);
}
