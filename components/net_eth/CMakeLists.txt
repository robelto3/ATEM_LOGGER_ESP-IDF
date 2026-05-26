idf_component_register(
    SRCS "net_eth.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_eth esp_netif esp_event esp_common net_config
)
