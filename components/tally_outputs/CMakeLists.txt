idf_component_register(
    SRCS "tally_outputs.c"
    INCLUDE_DIRS "include"
    REQUIRES app_state net_config esp_driver_gpio esp_common
)
