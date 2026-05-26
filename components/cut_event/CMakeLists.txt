idf_component_register(
    SRCS "cut_event.c"
    INCLUDE_DIRS "include"
    REQUIRES app_state edl_writer debug_control ltc freertos esp_common
)
