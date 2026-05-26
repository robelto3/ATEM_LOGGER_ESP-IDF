idf_component_register(
    SRCS "logger_session.c"
    INCLUDE_DIRS "include"
    REQUIRES app_state edl_writer file_protect rtc sd_storage show_config esp_common debug_control
)
