#include "tally_outputs.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "app_state.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "net_config.h"
#include "tally_outputs_pins.h"

// =====================================================
// Přehled pinů je v include/tally_outputs_pins.h
// =====================================================

static const gpio_num_t s_program_pins[TALLY_OUTPUT_INPUT_COUNT] = TALLY_OUTPUT_PROGRAM_PINS;
static const gpio_num_t s_preview_pins[TALLY_OUTPUT_INPUT_COUNT] = TALLY_OUTPUT_PREVIEW_PINS;

static bool s_initialized = false;

static uint64_t tally_outputs_make_pin_mask(void)
{
    uint64_t mask = 0;

    for (uint8_t i = 0; i < TALLY_OUTPUT_INPUT_COUNT; i++) {
        mask |= (1ULL << s_program_pins[i]);
        mask |= (1ULL << s_preview_pins[i]);
    }

    return mask;
}

static void tally_outputs_set_pin(gpio_num_t gpio, bool active)
{
    gpio_set_level(gpio, active ? TALLY_OUTPUT_ACTIVE_LEVEL : TALLY_OUTPUT_INACTIVE_LEVEL);
}

static void tally_outputs_apply(uint8_t program_input, uint8_t preview_input, bool program_tally_enabled, bool preview_tally_enabled)
{
    for (uint8_t i = 0; i < TALLY_OUTPUT_INPUT_COUNT; i++) {
        uint8_t input_number = (uint8_t)(i + 1);

        if (program_tally_enabled) {
            tally_outputs_set_pin(s_program_pins[i], program_input == input_number);
        } else {
            tally_outputs_set_pin(s_program_pins[i], false);
        }

        if (preview_tally_enabled) {
            tally_outputs_set_pin(s_preview_pins[i], preview_input == input_number);
        } else {
            tally_outputs_set_pin(s_preview_pins[i], false);
        }
    }
}

esp_err_t tally_outputs_init(void)
{
    uint64_t pin_mask = tally_outputs_make_pin_mask();

    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        return ret;
    }

    s_initialized = true;
    tally_outputs_all_off();

    return ESP_OK;
}

void tally_outputs_update(void)
{
    if (!s_initialized) {
        return;
    }

    app_state_snapshot_t state;
    app_state_get_snapshot(&state);

#if !TALLY_OUTPUT_SHOW_WITHOUT_ATEM
    // Přísný ostrý režim:
    // při ztrátě ATEM spojení zhasneme všechny tally výstupy,
    // aby na LEDkách nezůstal viset starý Program/Preview stav.
    if (!state.atem_connected) {
        tally_outputs_all_off();
        return;
    }
#endif

    // Testovací/domácí režim:
    // když je TALLY_OUTPUT_SHOW_WITHOUT_ATEM = 1, výstupy sledují app_state
    // i bez ATEM spojení. Díky tomu funguje test tally přes fake cut GPIO46.
    // Program i Preview tally lze vypnout z webu.
    bool program_tally_enabled = net_config_get_program_tally_enabled();
    bool preview_tally_enabled = net_config_get_preview_tally_enabled();
    tally_outputs_apply(state.program_input, state.preview_input, program_tally_enabled, preview_tally_enabled);
}

void tally_outputs_all_off(void)
{
    if (!s_initialized) {
        return;
    }

    for (uint8_t i = 0; i < TALLY_OUTPUT_INPUT_COUNT; i++) {
        tally_outputs_set_pin(s_program_pins[i], false);
        tally_outputs_set_pin(s_preview_pins[i], false);
    }
}
