#include "app_state.h"

#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static app_state_snapshot_t s_state;
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;

static uint32_t app_state_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}


static ltc_time_t app_state_make_output_tc(const ltc_time_t *ltc)
{
    ltc_time_t out = {0};

    if (!ltc) {
        return out;
    }

    out = *ltc;

    // LTC vstup běží 25 fps, ale pro display/eventy/EDL chceme
    // sudý výstupní frame v rozsahu 00..48.
    // Žádné liché mezisnímky nedopočítáváme.
    out.frames = (uint8_t)(out.frames * APP_STATE_LTC_OUTPUT_FRAME_MULTIPLIER);

    return out;
}

static void app_state_copy_filename_locked(const char *filename)
{
    if (!filename) {
        s_state.current_filename[0] = '\0';
        return;
    }

    strncpy(s_state.current_filename, filename, APP_STATE_FILENAME_MAX_LEN - 1);
    s_state.current_filename[APP_STATE_FILENAME_MAX_LEN - 1] = '\0';
}

esp_err_t app_state_init(void)
{
    uint32_t now_ms = app_state_now_ms();

    portENTER_CRITICAL(&s_state_mux);

    memset(&s_state, 0, sizeof(s_state));
    app_state_copy_filename_locked("NOFILE.edl");
    s_state.updated_ms = now_ms;

    portEXIT_CRITICAL(&s_state_mux);

    return ESP_OK;
}

void app_state_update_rtc(const rtc_datetime_t *rtc, bool valid)
{
    uint32_t now_ms = app_state_now_ms();

    portENTER_CRITICAL(&s_state_mux);

    s_state.rtc_valid = valid;

    if (rtc && valid) {
        s_state.rtc = *rtc;
    }

    s_state.updated_ms = now_ms;

    portEXIT_CRITICAL(&s_state_mux);
}

void app_state_update_ltc(const ltc_time_t *ltc)
{
    uint32_t now_ms = app_state_now_ms();

    portENTER_CRITICAL(&s_state_mux);

    if (ltc) {
        s_state.tc = app_state_make_output_tc(ltc);
        s_state.ltc_valid = ltc->valid;
    } else {
        memset(&s_state.tc, 0, sizeof(s_state.tc));
        s_state.ltc_valid = false;
    }

    s_state.updated_ms = now_ms;

    portEXIT_CRITICAL(&s_state_mux);
}

void app_state_set_atem_connected(bool connected)
{
    uint32_t now_ms = app_state_now_ms();

    portENTER_CRITICAL(&s_state_mux);

    s_state.atem_connected = connected;
    s_state.updated_ms = now_ms;

    portEXIT_CRITICAL(&s_state_mux);
}

void app_state_set_program_preview(uint8_t program_input, uint8_t preview_input)
{
    uint32_t now_ms = app_state_now_ms();

    portENTER_CRITICAL(&s_state_mux);

    s_state.program_input = program_input;
    s_state.preview_input = preview_input;
    s_state.updated_ms = now_ms;

    portEXIT_CRITICAL(&s_state_mux);
}

void app_state_set_current_filename(const char *filename)
{
    uint32_t now_ms = app_state_now_ms();

    portENTER_CRITICAL(&s_state_mux);

    app_state_copy_filename_locked(filename);
    s_state.updated_ms = now_ms;

    portEXIT_CRITICAL(&s_state_mux);
}

void app_state_get_snapshot(app_state_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }

    portENTER_CRITICAL(&s_state_mux);
    *snapshot = s_state;
    portEXIT_CRITICAL(&s_state_mux);
}
