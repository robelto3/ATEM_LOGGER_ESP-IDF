idf_component_register(
    SRCS "ltc_input.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_timer esp_common debug_control
)