#include "display.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_state.h"
#include "ssd1306.h"
#include "i2c_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DISPLAY_OLED_ADDR 0x3C
#define DISPLAY_STARTUP_HOLD_MS 3000

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

    snprintf(line_server, sizeof(line_server), "ESP  %s", server_ip);
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
    char line_file[24];
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

    snprintf(
        line_file,
        sizeof(line_file),
        "FILE %.16s",
        state.current_filename
    );

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

    // Spodní řádek se souborem vystředěný.
    display_print_centered(57, line_file, 1);

    return ssd1306_show();
}
