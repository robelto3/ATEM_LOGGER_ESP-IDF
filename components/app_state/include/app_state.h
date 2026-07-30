#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "rtc.h"
#include "ltc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_STATE_FILENAME_MAX_LEN 32

// Vstupní LTC je 25 fps. Pro 50fps výstupní TC se frame násobí 2
// na sudé hodnoty 00, 02, 04 ... 48. Pro 25fps výstup se nechá přímo.
#define APP_STATE_LTC_OUTPUT_FRAME_MULTIPLIER 2

typedef struct {
    bool atem_connected;
    bool ltc_valid;
    bool ltc_format_error;
    bool rtc_valid;

    uint8_t program_input;
    uint8_t preview_input;

    rtc_datetime_t rtc;
    // Výstupní timecode pro display/eventy/EDL.
    // Frame je podle nastavení TC Out buď 25fps, nebo 50fps sudý.
    ltc_time_t tc;

    char current_filename[APP_STATE_FILENAME_MAX_LEN];

    uint32_t updated_ms;
} app_state_snapshot_t;

esp_err_t app_state_init(void);

void app_state_update_rtc(const rtc_datetime_t *rtc, bool valid);
void app_state_update_ltc(const ltc_time_t *ltc);

void app_state_set_atem_connected(bool connected);
void app_state_set_program_preview(uint8_t program_input, uint8_t preview_input);
void app_state_set_current_filename(const char *filename);

void app_state_get_snapshot(app_state_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
