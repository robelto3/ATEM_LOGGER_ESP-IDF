#include "display.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_state.h"
#include "cut_event.h"
#include "ssd1306.h"
#include "i2c_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DISPLAY_OLED_ADDR 0x3C
#define DISPLAY_STARTUP_HOLD_MS 5000

static bool s_display_ready = false;
static TickType_t s_startup_screen_until = 0;

static const char *display_status_text(bool ok)
{
    return ok ? "OK" : "---";
}

static int display_text_width_scaled(const char *text, uint8_t scale)
{
    if (!text || text[0] == '\0') {
        return 0;
    }

    if (scale == 0) {
        scale = 1;
    }

    const int len = (int)strlen(text);
    const int glyph_w = 5 * scale;
    const int advance = glyph_w + 1;

    return ((len - 1) * advance) + glyph_w;
}

static int display_center_x(const char *text, uint8_t scale, int area_x, int area_w)
{
    const int text_w = display_text_width_scaled(text, scale);
    int x = area_x + ((area_w - text_w) / 2);

    if (x < area_x) {
        x = area_x;
    }

    return x;
}

static void display_print_centered(int y, const char *text, uint8_t scale)
{
    const int x = display_center_x(text, scale, 0, SSD1306_WIDTH);
    ssd1306_print_scaled(x, y, text, scale);
}

static void display_print_bus_value(int area_x, const char *label, const char *value)
{
    const uint8_t label_scale = 1;
    const uint8_t value_scale = 2;
    const int gap = 3;

    const int label_w = display_text_width_scaled(label, label_scale);
    const int value_w = display_text_width_scaled(value, value_scale);
    const int block_w = label_w + gap + value_w;

    int x = area_x + ((64 - block_w) / 2);
    if (x < area_x) {
        x = area_x;
    }

    // Malý popisek a velké číslo jsou v jednom řádku.
    // Y souřadnice jsou nastavené tak, aby byly opticky zarovnané na střed.
    ssd1306_print_scaled(x, 17, label, label_scale);
    ssd1306_print_scaled(x + label_w + gap, 13, value, value_scale);
}

static void display_format_uint_spaces(uint32_t value, char *out, size_t out_size)
{
    char raw[16];
    char formatted[24];
    size_t raw_len;
    size_t out_pos = 0;

    if (!out || out_size == 0U) {
        return;
    }

    snprintf(raw, sizeof(raw), "%lu", (unsigned long)value);
    raw_len = strlen(raw);

    for (size_t i = 0; i < raw_len && out_pos + 1U < sizeof(formatted); i++) {
        size_t remaining = raw_len - i;
        if (i > 0U && (remaining % 3U) == 0U) {
            formatted[out_pos++] = ' ';
            if (out_pos + 1U >= sizeof(formatted)) {
                break;
            }
        }
        formatted[out_pos++] = raw[i];
    }

    formatted[out_pos] = '\0';
    snprintf(out, out_size, "%s", formatted);
}

esp_err_t display_init(void)
{
    esp_err_t ret = ssd1306_init_with_bus(
        i2c_bus_get_handle(),
        DISPLAY_OLED_ADDR,
        400000
    );

    if (ret == ESP_OK) {
        s_display_ready = true;
    }

    return ret;
}

esp_err_t display_clear(void)
{
    if (!s_display_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ssd1306_clear();
    if (ret != ESP_OK) {
        return ret;
    }

    return ssd1306_show();
}

// =====================================================
// Obrazovky displeje
// Main neposílá konkrétní texty ani hodnoty na OLED.
// Display si vezme společný snapshot z app_state.
// =====================================================

esp_err_t display_show_startup_screen(const char *server_ip, const char *atem_ip)
{
    if (!s_display_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if ((server_ip == NULL) || (server_ip[0] == '\0')) {
        server_ip = "---";
    }

    if ((atem_ip == NULL) || (atem_ip[0] == '\0')) {
        atem_ip = "---";
    }

    char line_server[24];
    char line_atem[24];

    snprintf(line_server, sizeof(line_server), "Logger %s", server_ip);
    snprintf(line_atem, sizeof(line_atem), "ATEM %s", atem_ip);

    esp_err_t ret = ssd1306_clear();
    if (ret != ESP_OK) {
        return ret;
    }

    display_print_centered(0, "ATEM LOGGER", 1);
    display_print_centered(18, "START IP", 1);
    display_print_centered(32, line_server, 1);
    display_print_centered(46, line_atem, 1);

    ret = ssd1306_show();
    if (ret == ESP_OK) {
        s_startup_screen_until = xTaskGetTickCount() + pdMS_TO_TICKS(DISPLAY_STARTUP_HOLD_MS);
    }

    return ret;
}

esp_err_t display_show_main_screen(void)
{
    if (!s_display_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_startup_screen_until != 0) {
        TickType_t now = xTaskGetTickCount();
        if ((int32_t)(s_startup_screen_until - now) > 0) {
            return ESP_OK;
        }
        s_startup_screen_until = 0;
    }

    app_state_snapshot_t state;
    app_state_get_snapshot(&state);

    char line_status[24];
    char line_cuts[32];
    char cut_count_text[16];
    char pgm_text[8];
    char pvw_text[8];
    char tc_text[16];

    snprintf(
        line_status,
        sizeof(line_status),
        "ATEM:%s    LTC:%s",
        display_status_text(state.atem_connected),
        display_status_text(state.ltc_valid)
    );

    snprintf(pgm_text, sizeof(pgm_text), "%u", state.program_input);
    snprintf(pvw_text, sizeof(pvw_text), "%u", state.preview_input);

    if (state.ltc_valid) {
        snprintf(
            tc_text,
            sizeof(tc_text),
            "%02u:%02u:%02u:%02u",
            state.tc.hours,
            state.tc.minutes,
            state.tc.seconds,
            state.tc.frames
        );
    } else {
        snprintf(tc_text, sizeof(tc_text), "--:--:--:--");
    }

    display_format_uint_spaces(cut_event_get_cut_count(), cut_count_text, sizeof(cut_count_text));
    snprintf(line_cuts, sizeof(line_cuts), "Pocet strihu: %s", cut_count_text);

    esp_err_t ret = ssd1306_clear();
    if (ret != ESP_OK) {
        return ret;
    }

    // Horní stavový řádek vystředěný.
    display_print_centered(0, line_status, 1);

    // Program / Preview: popisek a číslo v jednom řádku, opticky na střed.
    display_print_bus_value(0, "PGM ", pgm_text);
    display_print_bus_value(64, "PVW ", pvw_text);

    // TCx2 zůstává velký, jen jemně vystředěný.
    display_print_centered(34, tc_text, 2);

    // Spodní řádek s počtem střihů aktuální EDL session.
    display_print_centered(57, line_cuts, 1);

    return ssd1306_show();
}
