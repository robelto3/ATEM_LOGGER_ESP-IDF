#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Ethernet pro ESP32-P4-ETH.
// Používá interní ESP32-P4 EMAC + externí RMII PHY přes generic PHY driver.
// IP adresa ESP/web serveru se bere z komponenty net_config.

typedef struct {
    bool initialized;
    bool link_up;
    bool got_ip;
    char ip[16];
    char netmask[16];
} net_eth_status_t;

esp_err_t net_eth_init_static(void);
void net_eth_get_status(net_eth_status_t *status);

#ifdef __cplusplus
}
#endif
