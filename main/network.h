#ifndef _NETWORK_H
#define _NETWORK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_netif.h"

#include "sdkconfig.h"

typedef enum { 
	NETWORK_TYPE_WIFI, 
#if CONFIG_EXAMPLE_CONNECT_ETHERNET
	NETWORK_TYPE_ETH 
#endif
} network_type_e;

void network_initialize(network_type_e type);
network_type_e get_network_type();
esp_err_t network_connect(void);
esp_err_t network_disconnect(void);

esp_ip4_addr_t network_get_ip();
esp_ip4_addr_t network_get_gateway();


/**
 * @brief Returns esp-netif pointer created by example_connect()
 *
 * @note If multiple interfaces active at once, this API return NULL
 * In that case the get_example_netif_from_desc() should be used
 * to get esp-netif pointer based on interface description
 */
esp_netif_t *get_network_netif(void);

/**
 * @brief Returns esp-netif pointer created by example_connect() described by
 * the supplied desc field
 *
 * @param desc Textual interface of created network interface, for example "sta"
 * indicate default WiFi station, "eth" default Ethernet interface.
 *
 */
esp_netif_t *get_network_netif_from_desc(const char *desc);

void wifi_load_config();
void wifi_save_config();
void wifi_factory_reset();

void wifi_scan_start(char *ssid, uint8_t *bssid, uint8_t channel);

void wifi_set_ssid(char *ssid);
const char *wifi_get_ssid();

void wifi_set_password(char *pass);
const char *wifi_get_password();

void wifi_set_ap_ssid(char *ssid);
const char *wifi_get_ap_ssid();

void wifi_set_ap_password(char *pass);
const char *wifi_get_ap_password();

void wifi_set_ap_channel(uint8_t ch);
uint8_t wifi_get_ap_channel();

void wifi_set_static_ip(esp_ip4_addr_t *ip4);
esp_ip4_addr_t wifi_get_static_ip();

void wifi_set_static_gateway(esp_ip4_addr_t *ip4);
esp_ip4_addr_t wifi_get_static_gateway();

void wifi_set_static_netmask(esp_ip4_addr_t *ip4);
esp_ip4_addr_t wifi_get_static_netmask();

typedef enum {
	WIFI_DISCONNECTED,
	WIFI_CONNECTED,
	WIFI_CONNECTING,
	WIFI_CONNECT_FAIL,
	WIFI_INTERNAL_ERROR
} wifi_status_t;

wifi_status_t wifi_get_status();

void eth_load_config();
void eth_save_config();
void eth_factory_reset();

void eth_set_static_ip(esp_ip4_addr_t *ip4);
esp_ip4_addr_t eth_get_static_ip();

void eth_set_static_gateway(esp_ip4_addr_t *ip4);
esp_ip4_addr_t eth_get_static_gateway();

typedef enum {
	ETH_DISCONNECTED,
	ETH_CONNECTED,
	ETH_CONNECTING,
	ETH_CONNECT_FAIL,
	ETH_INTERNAL_ERROR
} eth_status_t;

eth_status_t eth_get_status();

#ifdef __cplusplus
}
#endif

#endif
