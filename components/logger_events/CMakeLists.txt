idf_component_register(
    SRCS "logger_events.c"
    INCLUDE_DIRS "include"
    REQUIRES cut_event rtc app_state logger_session debug_control freertos esp_timer esp_common
)
