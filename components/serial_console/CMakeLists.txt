idf_component_register(
    SRCS "serial_console.c"
    INCLUDE_DIRS "include"
    REQUIRES net_config debug_control esp_system esp_common freertos
)
