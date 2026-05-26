idf_component_register(
    SRCS "ssd1306.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common esp_driver_i2c
)