idf_component_register(
    SRCS "fake_cut_button.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_timer esp_common
)
