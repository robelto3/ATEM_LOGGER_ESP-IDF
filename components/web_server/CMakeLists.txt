idf_component_register(
    SRCS "web_server.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server sd_storage app_state net_config net_eth show_config logger_session cut_event logger_events rtc esp_common esp_system freertos
)
