#include "new_file_button.h"

#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_timer.h"

#define NEW_FILE_DEBOUNCE_MS      40
#define NEW_FILE_ACTIVE_LEVEL     0

// Diagnostika surových změn GPIO5.
// Běžně vypnuto, protože při odladěném tlačítku zbytečně zahlcuje monitor.
#ifndef NEW_FILE_BUTTON_RAW_DEBUG
#define NEW_FILE_BUTTON_RAW_DEBUG 0
#endif

static int s_last_raw_level = 1;
static int s_stable_level = 1;
static uint32_t s_last_raw_change_ms = 0;
#if NEW_FILE_BUTTON_RAW_DEBUG
static int s_debug_last_printed_level = -1;
#endif

static uint32_t new_file_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

esp_err_t new_file_button_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << NEW_FILE_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        return ret;
    }

    int level = gpio_get_level(NEW_FILE_BUTTON_GPIO);
    s_last_raw_level = level;
    s_stable_level = level;
    s_last_raw_change_ms = new_file_now_ms();

#if NEW_FILE_BUTTON_RAW_DEBUG
    s_debug_last_printed_level = level;
    printf("NEW FILE GPIO%d init level=%d  active=%d\n", NEW_FILE_BUTTON_GPIO, level, NEW_FILE_ACTIVE_LEVEL);
#endif

    return ESP_OK;
}

bool new_file_button_was_pressed(void)
{
    uint32_t now_ms = new_file_now_ms();
    int raw_level = gpio_get_level(NEW_FILE_BUTTON_GPIO);

#if NEW_FILE_BUTTON_RAW_DEBUG
    if (raw_level != s_debug_last_printed_level) {
        s_debug_last_printed_level = raw_level;
        printf("NEW FILE GPIO%d raw=%d\n", NEW_FILE_BUTTON_GPIO, raw_level);
    }
#endif

    if (raw_level != s_last_raw_level) {
        s_last_raw_level = raw_level;
        s_last_raw_change_ms = now_ms;
        return false;
    }

    if ((now_ms - s_last_raw_change_ms) < NEW_FILE_DEBOUNCE_MS) {
        return false;
    }

    if (raw_level == s_stable_level) {
        return false;
    }

    s_stable_level = raw_level;

    bool pressed = (s_stable_level == NEW_FILE_ACTIVE_LEVEL);
#if NEW_FILE_BUTTON_RAW_DEBUG
    if (pressed) {
        printf("NEW FILE GPIO%d debounced PRESS\n", NEW_FILE_BUTTON_GPIO);
    }
#endif

    return pressed;
}
