#include "debug_control.h"

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_log.h"

static portMUX_TYPE s_debug_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_debug_enabled = false;

void debug_control_init(void)
{
    portENTER_CRITICAL(&s_debug_mux);
    s_debug_enabled = false;
    portEXIT_CRITICAL(&s_debug_mux);

    esp_log_level_set("*", ESP_LOG_NONE);
}

void debug_control_set_enabled(bool enabled)
{
    portENTER_CRITICAL(&s_debug_mux);
    s_debug_enabled = enabled;
    portEXIT_CRITICAL(&s_debug_mux);

    esp_log_level_set("*", enabled ? ESP_LOG_INFO : ESP_LOG_NONE);
}

bool debug_control_is_enabled(void)
{
    bool enabled;

    portENTER_CRITICAL(&s_debug_mux);
    enabled = s_debug_enabled;
    portEXIT_CRITICAL(&s_debug_mux);

    return enabled;
}
