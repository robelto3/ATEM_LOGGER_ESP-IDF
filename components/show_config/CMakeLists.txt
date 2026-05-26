idf_component_register(
    SRCS "show_config.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_common freertos
)
