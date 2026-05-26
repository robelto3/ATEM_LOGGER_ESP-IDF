#include "net_eth.h"

#include <stdio.h>
#include <string.h>

#include "esp_eth.h"
#include "esp_eth_mac_esp.h"
#include "esp_eth_phy.h"
#include "esp_eth_netif_glue.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "lwip/ip4_addr.h"
#include "net_config.h"

// =====================================================
// ESP32-P4-ETH onboard Ethernet podle Waveshare schématu
// ESP-IDF v6.0.1 zde nepoužívá speciální IP101 konstruktor,
// proto bereme PHY přes obecný 802.3/generic PHY driver.
// =====================================================

#define NET_ETH_PHY_ADDR      1
#define NET_ETH_PHY_RST_GPIO  51

#define NET_ETH_MDC_GPIO      31
#define NET_ETH_MDIO_GPIO     52

#define NET_ETH_RMII_CLK_GPIO 50

#define NET_ETH_RMII_TX_EN    49
#define NET_ETH_RMII_TXD0     34
#define NET_ETH_RMII_TXD1     35
#define NET_ETH_RMII_CRS_DV   28
#define NET_ETH_RMII_RXD0     29
#define NET_ETH_RMII_RXD1     30

static const char *TAG = "NET_ETH";

static esp_netif_t *s_eth_netif = NULL;
static esp_eth_handle_t s_eth_handle = NULL;
static esp_eth_netif_glue_handle_t s_eth_glue = NULL;

static net_eth_status_t s_status;
static portMUX_TYPE s_status_mux = portMUX_INITIALIZER_UNLOCKED;

static void net_eth_copy_ip_strings_locked(const esp_netif_ip_info_t *ip_info)
{
    if (!ip_info) {
        net_config_get_server_ip_string(s_status.ip, sizeof(s_status.ip));
        net_config_get_netmask_string(s_status.netmask, sizeof(s_status.netmask));
        net_config_get_gateway_string(s_status.gateway, sizeof(s_status.gateway));
        return;
    }

    snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(&ip_info->ip));
    snprintf(s_status.netmask, sizeof(s_status.netmask), IPSTR, IP2STR(&ip_info->netmask));
    snprintf(s_status.gateway, sizeof(s_status.gateway), IPSTR, IP2STR(&ip_info->gw));
}

static void net_eth_event_handler(void *arg,
                                  esp_event_base_t event_base,
                                  int32_t event_id,
                                  void *event_data)
{
    (void)arg;
    (void)event_base;

    uint8_t mac_addr[6] = {0};
    esp_eth_handle_t eth_handle = NULL;

    if (event_data) {
        eth_handle = *(esp_eth_handle_t *)event_data;
    }

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        if (eth_handle) {
            esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        }

        portENTER_CRITICAL(&s_status_mux);
        s_status.link_up = true;
        portEXIT_CRITICAL(&s_status_mux);

        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(
            TAG,
            "Ethernet MAC %02x:%02x:%02x:%02x:%02x:%02x",
            mac_addr[0], mac_addr[1], mac_addr[2],
            mac_addr[3], mac_addr[4], mac_addr[5]
        );
        break;

    case ETHERNET_EVENT_DISCONNECTED:
        portENTER_CRITICAL(&s_status_mux);
        s_status.link_up = false;
        s_status.got_ip = false;
        portEXIT_CRITICAL(&s_status_mux);

        ESP_LOGI(TAG, "Ethernet Link Down");
        break;

    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;

    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;

    default:
        break;
    }
}

static void net_eth_got_ip_handler(void *arg,
                                   esp_event_base_t event_base,
                                   int32_t event_id,
                                   void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_id;

    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    portENTER_CRITICAL(&s_status_mux);
    s_status.got_ip = true;
    net_eth_copy_ip_strings_locked(ip_info);
    portEXIT_CRITICAL(&s_status_mux);

    ESP_LOGI(TAG, "Ethernet Got IP");
    ESP_LOGI(TAG, "IP:      " IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&ip_info->gw));
}

static esp_err_t net_eth_create_driver(void)
{
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();

    emac_config.smi_gpio.mdc_num = NET_ETH_MDC_GPIO;
    emac_config.smi_gpio.mdio_num = NET_ETH_MDIO_GPIO;

    emac_config.interface = EMAC_DATA_INTERFACE_RMII;
    emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac_config.clock_config.rmii.clock_gpio = NET_ETH_RMII_CLK_GPIO;

    emac_config.emac_dataif_gpio.rmii.tx_en_num = NET_ETH_RMII_TX_EN;
    emac_config.emac_dataif_gpio.rmii.txd0_num = NET_ETH_RMII_TXD0;
    emac_config.emac_dataif_gpio.rmii.txd1_num = NET_ETH_RMII_TXD1;
    emac_config.emac_dataif_gpio.rmii.crs_dv_num = NET_ETH_RMII_CRS_DV;
    emac_config.emac_dataif_gpio.rmii.rxd0_num = NET_ETH_RMII_RXD0;
    emac_config.emac_dataif_gpio.rmii.rxd1_num = NET_ETH_RMII_RXD1;

    phy_config.phy_addr = NET_ETH_PHY_ADDR;
    phy_config.reset_gpio_num = NET_ETH_PHY_RST_GPIO;

    ESP_LOGI(TAG, "init ESP32-P4-ETH internal EMAC + generic RMII PHY");
    ESP_LOGI(
        TAG,
        "pins MDC=%d MDIO=%d RST=%d CLK=%d TX_EN=%d TXD0=%d TXD1=%d CRS_DV=%d RXD0=%d RXD1=%d",
        NET_ETH_MDC_GPIO,
        NET_ETH_MDIO_GPIO,
        NET_ETH_PHY_RST_GPIO,
        NET_ETH_RMII_CLK_GPIO,
        NET_ETH_RMII_TX_EN,
        NET_ETH_RMII_TXD0,
        NET_ETH_RMII_TXD1,
        NET_ETH_RMII_CRS_DV,
        NET_ETH_RMII_RXD0,
        NET_ETH_RMII_RXD1
    );

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    if (!mac) {
        ESP_LOGE(TAG, "esp_eth_mac_new_esp32 failed");
        return ESP_FAIL;
    }

    esp_eth_phy_t *phy = esp_eth_phy_new_generic(&phy_config);
    if (!phy) {
        ESP_LOGE(TAG, "esp_eth_phy_new_generic failed");
        mac->del(mac);
        return ESP_FAIL;
    }

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_err_t ret = esp_eth_driver_install(&eth_config, &s_eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_eth_driver_install failed: %s", esp_err_to_name(ret));
        phy->del(phy);
        mac->del(mac);
        s_eth_handle = NULL;
        return ret;
    }

    return ESP_OK;
}

static esp_err_t net_eth_attach_static_ip(void)
{
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();

    s_eth_netif = esp_netif_new(&netif_config);
    if (!s_eth_netif) {
        ESP_LOGE(TAG, "esp_netif_new failed");
        return ESP_FAIL;
    }

    s_eth_glue = esp_eth_new_netif_glue(s_eth_handle);
    if (!s_eth_glue) {
        ESP_LOGE(TAG, "esp_eth_new_netif_glue failed");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_netif_attach(s_eth_netif, s_eth_glue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_attach failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_netif_dhcpc_stop(s_eth_netif);
    if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(TAG, "dhcp stop failed: %s", esp_err_to_name(ret));
    }

    net_config_ip4_t server_ip = {0};
    net_config_ip4_t netmask = {0};
    net_config_ip4_t gateway = {0};

    net_config_get_server_ip(&server_ip);
    net_config_get_netmask(&netmask);
    net_config_get_gateway(&gateway);

    esp_netif_ip_info_t ip_info = {0};
    IP4_ADDR(&ip_info.ip, server_ip.a, server_ip.b, server_ip.c, server_ip.d);
    IP4_ADDR(&ip_info.netmask, netmask.a, netmask.b, netmask.c, netmask.d);
    IP4_ADDR(&ip_info.gw, gateway.a, gateway.b, gateway.c, gateway.d);

    ret = esp_netif_set_ip_info(s_eth_netif, &ip_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set static IP failed: %s", esp_err_to_name(ret));
        return ret;
    }

    portENTER_CRITICAL(&s_status_mux);
    net_eth_copy_ip_strings_locked(&ip_info);
    portEXIT_CRITICAL(&s_status_mux);

    char server_ip_str[NET_CONFIG_IP_STR_LEN] = {0};
    net_config_get_server_ip_string(server_ip_str, sizeof(server_ip_str));
    ESP_LOGI(TAG, "static IP configured: %s", server_ip_str);
    return ESP_OK;
}

esp_err_t net_eth_init_static(void)
{
    if (s_eth_handle != NULL) {
        return ESP_OK;
    }

    memset(&s_status, 0, sizeof(s_status));
    net_config_get_server_ip_string(s_status.ip, sizeof(s_status.ip));
    net_config_get_netmask_string(s_status.netmask, sizeof(s_status.netmask));
    net_config_get_gateway_string(s_status.gateway, sizeof(s_status.gateway));

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = net_eth_create_driver();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = net_eth_attach_static_ip();
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &net_eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &net_eth_got_ip_handler, NULL));

    ret = esp_eth_start(s_eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_eth_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    portENTER_CRITICAL(&s_status_mux);
    s_status.initialized = true;
    portEXIT_CRITICAL(&s_status_mux);

    char server_ip_str[NET_CONFIG_IP_STR_LEN] = {0};
    net_config_get_server_ip_string(server_ip_str, sizeof(server_ip_str));
    ESP_LOGI(TAG, "Ethernet init OK, web IP will be http://%s/", server_ip_str);
    return ESP_OK;
}

void net_eth_get_status(net_eth_status_t *status)
{
    if (!status) {
        return;
    }

    portENTER_CRITICAL(&s_status_mux);
    *status = s_status;
    portEXIT_CRITICAL(&s_status_mux);
}
