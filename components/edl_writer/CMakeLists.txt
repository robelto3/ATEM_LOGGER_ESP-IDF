idf_component_register(
    SRCS "edl_writer.c"
    INCLUDE_DIRS "include"
    REQUIRES rtc ltc sd_storage debug_control esp_common
)
