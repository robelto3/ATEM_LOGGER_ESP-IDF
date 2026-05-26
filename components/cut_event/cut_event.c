#include "cut_event.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "app_state.h"
#include "edl_writer.h"
#include "debug_control.h"

// Jeden rozpracovaný programový úsek v RAM.
// Hotové EDL události zapisuje komponenta edl_writer.
typedef struct {
    bool active;
    uint8_t camera;
    ltc_time_t in_tc;
} cut_event_segment_t;

static uint32_t s_cut_count = 0;
static uint32_t s_edl_event_count = 0;
static cut_event_segment_t s_current_segment;
static portMUX_TYPE s_cut_event_mux = portMUX_INITIALIZER_UNLOCKED;

static void cut_event_debug_printf(const char *fmt, ...)
{
    if (!debug_control_is_enabled()) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}


esp_err_t cut_event_init(void)
{
    portENTER_CRITICAL(&s_cut_event_mux);

    s_cut_count = 0;
    s_edl_event_count = 0;
    memset(&s_current_segment, 0, sizeof(s_current_segment));

    portEXIT_CRITICAL(&s_cut_event_mux);

    return ESP_OK;
}

uint32_t cut_event_get_cut_count(void)
{
    uint32_t count;

    portENTER_CRITICAL(&s_cut_event_mux);
    count = s_cut_count;
    portEXIT_CRITICAL(&s_cut_event_mux);

    return count;
}

uint32_t cut_event_get_edl_event_count(void)
{
    uint32_t count;

    portENTER_CRITICAL(&s_cut_event_mux);
    count = s_edl_event_count;
    portEXIT_CRITICAL(&s_cut_event_mux);

    return count;
}

uint32_t cut_event_get_count(void)
{
    return cut_event_get_cut_count();
}

bool cut_event_close_active_segment_at_tc(const ltc_time_t *out_tc, bool out_tc_valid)
{
    if (out_tc == NULL) {
        out_tc_valid = false;
    }

    uint32_t edl_number_to_print = 0;
    cut_event_segment_t segment_to_print = {0};
    bool had_active_segment = false;

    portENTER_CRITICAL(&s_cut_event_mux);

    if (s_current_segment.active) {
        had_active_segment = true;

        if (out_tc_valid) {
            s_edl_event_count++;
            edl_number_to_print = s_edl_event_count;
            segment_to_print = s_current_segment;
        }

        // Ať už TC validní je nebo není, při ukončení session se starý
        // rozpracovaný segment nesmí přenést do nového souboru.
        memset(&s_current_segment, 0, sizeof(s_current_segment));
    }

    portEXIT_CRITICAL(&s_cut_event_mux);

    if (!had_active_segment) {
        cut_event_debug_printf("CUT EVENT: no active segment to close\n");
        return false;
    }

    if (!out_tc_valid) {
        cut_event_debug_printf("CUT EVENT: active segment closed without EDL event - TC invalid\n");
        return false;
    }

    cut_event_debug_printf(
        "CUT EVENT: closing active segment CAM%u for current EDL session\n",
        segment_to_print.camera
    );

    edl_writer_write_event(
        edl_number_to_print,
        segment_to_print.camera,
        &segment_to_print.in_tc,
        out_tc
    );

    return true;
}

bool cut_event_close_active_segment_from_state(void)
{
    app_state_snapshot_t state;
    app_state_get_snapshot(&state);

    return cut_event_close_active_segment_at_tc(&state.tc, state.ltc_valid);
}

void cut_event_sync_program_start_at_tc(uint8_t program_input,
                                        const ltc_time_t *start_tc,
                                        bool start_tc_valid)
{
    if (program_input == 0U) {
        cut_event_debug_printf("CUT EVENT: ATEM sync skipped - program input is 0\n");
        return;
    }

    app_state_snapshot_t state;
    app_state_get_snapshot(&state);

    if ((start_tc == NULL) || !start_tc_valid) {
        // ATEM stav do app_state patří i bez platného LTC, ale EDL segment
        // bez přesného TC nezakládáme.
        app_state_set_program_preview(program_input, state.preview_input);
        cut_event_debug_printf("CUT EVENT: ATEM sync CAM%u without segment - TC invalid\n", program_input);
        return;
    }

    bool was_active = false;
    uint8_t old_camera = 0;
    bool same_camera = false;

    portENTER_CRITICAL(&s_cut_event_mux);

    was_active = s_current_segment.active;
    old_camera = s_current_segment.camera;
    same_camera = was_active && (old_camera == program_input);

    if (!same_camera) {
        s_current_segment.active = true;
        s_current_segment.camera = program_input;
        s_current_segment.in_tc = *start_tc;
    }

    portEXIT_CRITICAL(&s_cut_event_mux);

    app_state_set_program_preview(program_input, state.preview_input);

    if (!was_active) {
        cut_event_debug_printf(
            "CUT EVENT: ATEM sync start CAM%u IN %02u:%02u:%02u:%02u\n",
            program_input,
            start_tc->hours,
            start_tc->minutes,
            start_tc->seconds,
            start_tc->frames
        );
        return;
    }

    if (same_camera) {
        cut_event_debug_printf("CUT EVENT: ATEM sync CAM%u - active segment kept\n", program_input);
        return;
    }

    cut_event_debug_printf(
        "CUT EVENT: ATEM resync CAM%u -> CAM%u without EDL event IN %02u:%02u:%02u:%02u\n",
        old_camera,
        program_input,
        start_tc->hours,
        start_tc->minutes,
        start_tc->seconds,
        start_tc->frames
    );
}

void cut_event_sync_program_start(uint8_t program_input)
{
    app_state_snapshot_t state;
    app_state_get_snapshot(&state);

    cut_event_sync_program_start_at_tc(program_input, &state.tc, state.ltc_valid);
}

void cut_event_reset_session(void)
{
    portENTER_CRITICAL(&s_cut_event_mux);

    s_cut_count = 0;
    s_edl_event_count = 0;
    memset(&s_current_segment, 0, sizeof(s_current_segment));

    portEXIT_CRITICAL(&s_cut_event_mux);

    cut_event_debug_printf("CUT EVENT: session counters reset, active segment cleared\n");
}

void cut_event_record_with_previous_at_tc(uint8_t old_program_input,
                                          uint8_t new_program_input,
                                          const ltc_time_t *cut_tc,
                                          bool cut_tc_valid)
{
    app_state_snapshot_t state;
    app_state_get_snapshot(&state);

    if (cut_tc == NULL) {
        cut_tc_valid = false;
    }

    uint32_t cut_number;
    uint32_t edl_number_to_print = 0;
    bool should_print_edl = false;
    bool should_print_segment_start = false;
    cut_event_segment_t segment_to_print = {0};

    portENTER_CRITICAL(&s_cut_event_mux);

    s_cut_count++;
    cut_number = s_cut_count;

    if (cut_tc_valid) {
        if (s_current_segment.active) {
            s_edl_event_count++;
            edl_number_to_print = s_edl_event_count;
            segment_to_print = s_current_segment;
            should_print_edl = true;
        }

        // Každý platný střih zároveň založí nový programový úsek.
        s_current_segment.active = true;
        s_current_segment.camera = new_program_input;
        s_current_segment.in_tc = *cut_tc;
        should_print_segment_start = true;
    }

    portEXIT_CRITICAL(&s_cut_event_mux);

    // PGM/PVW stav posuneme vždy, i když by LTC zrovna nebyl validní.
    // EDL segmenty se ale zakládají/uzavírají jen s validním TC.
    app_state_set_program_preview(new_program_input, state.preview_input);

    edl_writer_write_cut_debug(
        cut_number,
        old_program_input,
        new_program_input,
        cut_tc,
        cut_tc_valid
    );

    if (!cut_tc_valid) {
        edl_writer_write_invalid_tc_notice();
        return;
    }

    if (should_print_edl) {
        edl_writer_write_event(
            edl_number_to_print,
            segment_to_print.camera,
            &segment_to_print.in_tc,
            cut_tc
        );
    }

    if (should_print_segment_start) {
        edl_writer_write_segment_start(new_program_input, cut_tc);
    }
}

void cut_event_record_with_previous(uint8_t old_program_input, uint8_t new_program_input)
{
    app_state_snapshot_t state;
    app_state_get_snapshot(&state);

    cut_event_record_with_previous_at_tc(
        old_program_input,
        new_program_input,
        &state.tc,
        state.ltc_valid
    );
}

void cut_event_record(uint8_t new_program_input)
{
    app_state_snapshot_t state;
    app_state_get_snapshot(&state);

    cut_event_record_with_previous(state.program_input, new_program_input);
}
