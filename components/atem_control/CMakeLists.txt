idf_component_register(
    SRCS "atem_control.c"
    INCLUDE_DIRS "include"
    REQUIRES net_config app_state logger_events debug_control freertos esp_timer esp_common lwip
)
