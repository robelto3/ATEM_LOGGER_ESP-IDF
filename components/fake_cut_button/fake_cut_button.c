#include "fake_cut_button.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_timer.h"

#define FAKE_CUT_DEBOUNCE_MS   40
#define FAKE_CUT_ACTIVE_LEVEL  0

static int s_last_raw_level = 1;
static int s_stable_level = 1;
static uint32_t s_last_raw_change_ms = 0;

static uint32_t fake_cut_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

esp_err_t fake_cut_button_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << FAKE_CUT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        return ret;
    }

    int level = gpio_get_level(FAKE_CUT_BUTTON_GPIO);
    s_last_raw_level = level;
    s_stable_level = level;
    s_last_raw_change_ms = fake_cut_now_ms();

    return ESP_OK;
}

bool fake_cut_button_was_pressed(void)
{
    uint32_t now_ms = fake_cut_now_ms();
    int raw_level = gpio_get_level(FAKE_CUT_BUTTON_GPIO);

    if (raw_level != s_last_raw_level) {
        s_last_raw_level = raw_level;
        s_last_raw_change_ms = now_ms;
        return false;
    }

    if ((now_ms - s_last_raw_change_ms) < FAKE_CUT_DEBOUNCE_MS) {
        return false;
    }

    if (raw_level == s_stable_level) {
        return false;
    }

    s_stable_level = raw_level;

    return s_stable_level == FAKE_CUT_ACTIVE_LEVEL;
}
