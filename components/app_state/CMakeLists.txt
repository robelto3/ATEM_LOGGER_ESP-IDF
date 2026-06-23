idf_component_register(
    SRCS "app_state.c"
    INCLUDE_DIRS "include"
    REQUIRES rtc ltc net_config esp_timer freertos esp_common
)
