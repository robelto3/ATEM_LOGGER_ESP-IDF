idf_component_register(
    SRCS "display.c"
    INCLUDE_DIRS "include"
    REQUIRES ssd1306 i2c_bus app_state cut_event freertos esp_common
)
