idf_component_register(
    SRCS "sd_storage.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES fatfs sdmmc esp_driver_sdmmc esp_driver_gpio debug_control
)
