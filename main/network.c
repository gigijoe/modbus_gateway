#include <string.h>
#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#if CONFIG_EXAMPLE_CONNECT_ETHERNET
#include "esp_eth.h"
#if CONFIG_ETH_USE_SPI_ETHERNET
#include "driver/spi_master.h"
#endif // CONFIG_ETH_USE_SPI_ETHERNET
#endif // CONFIG_EXAMPLE_CONNECT_ETHERNET
#include "esp_log.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "dhcpserver/dhcpserver.h"
#include "lwip/lwip_napt.h"

#include "nvs_flash.h"

#include "esp_bt.h"
#include "esp_blufi_api.h"
#include "blufi_example.h"
#include "esp_blufi.h"

#include "network.h"
#include "led.h"

static network_type_e s_network_type = NETWORK_TYPE_NONE;
static wifi_status_t s_wifi_status = WIFI_STOPPED;
static eth_status_t s_eth_status = ETH_STOPPED;

static bool s_enable_blufi = false;
static bool s_ble_is_connected = false;

#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
#define MAX_IP6_ADDRS_PER_NETIF (5)

#if defined(CONFIG_EXAMPLE_CONNECT_IPV6_PREF_LOCAL_LINK)
#define EXAMPLE_CONNECT_PREFERRED_IPV6_TYPE ESP_IP6_ADDR_IS_LINK_LOCAL
#elif defined(CONFIG_EXAMPLE_CONNECT_IPV6_PREF_GLOBAL)
#define EXAMPLE_CONNECT_PREFERRED_IPV6_TYPE ESP_IP6_ADDR_IS_GLOBAL
#elif defined(CONFIG_EXAMPLE_CONNECT_IPV6_PREF_SITE_LOCAL)
#define EXAMPLE_CONNECT_PREFERRED_IPV6_TYPE ESP_IP6_ADDR_IS_SITE_LOCAL
#elif defined(CONFIG_EXAMPLE_CONNECT_IPV6_PREF_UNIQUE_LOCAL)
#define EXAMPLE_CONNECT_PREFERRED_IPV6_TYPE ESP_IP6_ADDR_IS_UNIQUE_LOCAL
#endif // if-elif CONFIG_EXAMPLE_CONNECT_IPV6_PREF_...

#else
#endif

#define EXAMPLE_DO_CONNECT CONFIG_EXAMPLE_CONNECT_WIFI || CONFIG_EXAMPLE_CONNECT_ETHERNET

#if CONFIG_EXAMPLE_WIFI_SCAN_METHOD_FAST
#define EXAMPLE_WIFI_SCAN_METHOD WIFI_FAST_SCAN
#elif CONFIG_EXAMPLE_WIFI_SCAN_METHOD_ALL_CHANNEL
#define EXAMPLE_WIFI_SCAN_METHOD WIFI_ALL_CHANNEL_SCAN
#endif

#if CONFIG_EXAMPLE_WIFI_CONNECT_AP_BY_SIGNAL
#define EXAMPLE_WIFI_CONNECT_AP_SORT_METHOD WIFI_CONNECT_AP_BY_SIGNAL
#elif CONFIG_EXAMPLE_WIFI_CONNECT_AP_BY_SECURITY
#define EXAMPLE_WIFI_CONNECT_AP_SORT_METHOD WIFI_CONNECT_AP_BY_SECURITY
#endif

#if CONFIG_EXAMPLE_WIFI_AUTH_OPEN
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_EXAMPLE_WIFI_AUTH_WEP
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_EXAMPLE_WIFI_AUTH_WPA_PSK
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_EXAMPLE_WIFI_AUTH_WPA2_PSK
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_EXAMPLE_WIFI_AUTH_WPA_WPA2_PSK
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_EXAMPLE_WIFI_AUTH_WPA2_ENTERPRISE
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_ENTERPRISE
#elif CONFIG_EXAMPLE_WIFI_AUTH_WPA3_PSK
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_EXAMPLE_WIFI_AUTH_WPA2_WPA3_PSK
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_EXAMPLE_WIFI_AUTH_WAPI_PSK
#define EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

static esp_netif_t *s_network_esp_netif = NULL;
static esp_netif_t *s_netif_sta = NULL;
static esp_netif_t *s_netif_ap = NULL;
static esp_netif_t *s_netif_eth = NULL;

#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
static esp_ip6_addr_t s_ipv6_addr;

/* types of ipv6 addresses to be displayed on ipv6 events */
static const char *s_ipv6_addr_types[] = {
    "ESP_IP6_ADDR_IS_UNKNOWN",
    "ESP_IP6_ADDR_IS_GLOBAL",
    "ESP_IP6_ADDR_IS_LINK_LOCAL",
    "ESP_IP6_ADDR_IS_SITE_LOCAL",
    "ESP_IP6_ADDR_IS_UNIQUE_LOCAL",
    "ESP_IP6_ADDR_IS_IPV4_MAPPED_IPV6"
};
#endif

static const char *TAG = "network";

#if CONFIG_EXAMPLE_CONNECT_WIFI
static esp_netif_t *wifi_start(void);
static void wifi_stop(void);
#endif
#if CONFIG_EXAMPLE_CONNECT_ETHERNET
static esp_netif_t *eth_start(void);
static void eth_stop(void);
#endif

/**
 * @brief Checks the netif description if it contains specified prefix.
 * All netifs created withing common connect component are prefixed with the module TAG,
 * so it returns true if the specified netif is owned by this module
 */
/*
static bool is_our_netif(const char *prefix, esp_netif_t *netif)
{
    return strncmp(prefix, esp_netif_get_desc(netif), strlen(prefix) - 1) == 0;
}
*/
extern esp_blufi_callbacks_t example_callbacks;

/* set up connection, Wi-Fi and/or Ethernet */
static void _network_start(void)
{
#if CONFIG_EXAMPLE_CONNECT_WIFI
	if(s_network_type == NETWORK_TYPE_WIFI)
    	s_network_esp_netif = wifi_start();
#endif

#if CONFIG_EXAMPLE_CONNECT_ETHERNET
	if(s_network_type == NETWORK_TYPE_ETH)
    	s_network_esp_netif = eth_start();
#endif

    if(s_enable_blufi) {
/****************************************************************************
* This is a demo for bluetooth config wifi connection to ap. You can config ESP32 to connect a softap
* or config ESP32 as a softap to be connected by other device. APP can be downloaded from github
* android source code: https://github.com/EspressifApp/EspBlufi
* iOS source code: https://github.com/EspressifApp/EspBlufiForiOS
****************************************************************************/

	    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

	    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
	    esp_err_t ret = esp_bt_controller_init(&bt_cfg);
	    if(ret != ESP_OK) {
	        BLUFI_ERROR("%s initialize bt controller failed: %s\n", __func__, esp_err_to_name(ret));
	    }

	    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
	    if(ret == ESP_OK) {
	        ret = esp_blufi_host_and_cb_init(&example_callbacks);
	        if(ret == ESP_OK) {
	            BLUFI_INFO("BLUFI VERSION %04x\n", esp_blufi_get_version());
	        } else {
	            BLUFI_ERROR("%s initialise failed: %s\n", __func__, esp_err_to_name(ret));
	        }
	    } else {    
	        BLUFI_ERROR("%s enable bt controller failed: %s\n", __func__, esp_err_to_name(ret));
	    }
    }
}

/* tear down connection, release resources */
static void _network_stop(void)
{
#if CONFIG_EXAMPLE_CONNECT_WIFI
	if(s_network_type == NETWORK_TYPE_WIFI)
    	wifi_stop();
#endif

#if CONFIG_EXAMPLE_CONNECT_ETHERNET
    if(s_network_type == NETWORK_TYPE_ETH)
    	eth_stop();
#endif
}

#if EXAMPLE_DO_CONNECT
static uint8_t s_conn_bssid[6];
static uint8_t s_conn_ssid[32] = {0};
static int s_conn_ssid_len;

static esp_ip4_addr_t s_ip_addr;
static esp_ip4_addr_t s_gateway_addr;

static void on_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
	ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
#if 0	
	if (!is_our_netif(TAG, event->esp_netif)) {
		ESP_LOGW(TAG, "Got IPv4 from another interface \"%s\": ignored", esp_netif_get_desc(event->esp_netif));
		return;
	}
#endif
	ESP_LOGI(TAG, "Got IPv4 event: Interface \"%s\" address: " IPSTR, esp_netif_get_desc(event->esp_netif), IP2STR(&event->ip_info.ip));
	memcpy(&s_ip_addr, &event->ip_info.ip, sizeof(s_ip_addr));
	memcpy(&s_gateway_addr, &event->ip_info.gw, sizeof(s_ip_addr));

#if CONFIG_EXAMPLE_CONNECT_WIFI
	esp_blufi_extra_info_t info;
	wifi_mode_t mode;
	esp_wifi_get_mode(&mode);
	memset(&info, 0, sizeof(esp_blufi_extra_info_t));
	memcpy(info.sta_bssid, s_conn_bssid, 6);
	info.sta_bssid_set = true;
	info.sta_ssid = s_conn_ssid;
	info.sta_ssid_len = s_conn_ssid_len;

	if (s_ble_is_connected == true) {
	    esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
	} else {
	    BLUFI_INFO("BLUFI BLE is not connected yet\n");
	}

	if(s_network_type == NETWORK_TYPE_WIFI) 
		s_wifi_status = WIFI_CONNECTED;
#endif

#if CONFIG_EXAMPLE_CONNECT_ETHERNET
	if(s_network_type == NETWORK_TYPE_ETH)
		s_eth_status = ETH_CONNECTED;
#endif
	led_on(&s_led25);
}
#endif

#ifdef CONFIG_EXAMPLE_CONNECT_IPV6

static void on_got_ipv6(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
/*    
    if (!is_our_netif(TAG, event->esp_netif)) {
        ESP_LOGW(TAG, "Got IPv6 from another netif: ignored");
        return;
    }
*/
    esp_ip6_addr_type_t ipv6_type = esp_netif_ip6_get_addr_type(&event->ip6_info.ip);
    ESP_LOGI(TAG, "Got IPv6 event: Interface \"%s\" address: " IPV6STR ", type: %s", esp_netif_get_desc(event->esp_netif),
             IPV62STR(event->ip6_info.ip), s_ipv6_addr_types[ipv6_type]);
    if (ipv6_type == EXAMPLE_CONNECT_PREFERRED_IPV6_TYPE) {
        memcpy(&s_ipv6_addr, &event->ip6_info.ip, sizeof(s_ipv6_addr));
    }
#if CONFIG_EXAMPLE_CONNECT_WIFI
    if(s_network_type == NETWORK_TYPE_WIFI)
		s_wifi_status = WIFI_CONNECTED;
#endif
}

#endif // CONFIG_EXAMPLE_CONNECT_IPV6

void network_initialize(network_type_e type, bool enable_blufi)
{
	s_network_type = type;
	s_enable_blufi = enable_blufi;
}

void network_deinitialize()
{
	s_network_type = NETWORK_TYPE_NONE;
}

network_type_e get_network_type()
{
	return s_network_type;
}

esp_err_t network_start(void)
{
    _network_start();
    ESP_ERROR_CHECK(esp_register_shutdown_handler(&_network_stop));
    return ESP_OK;
}

esp_err_t network_stop(void)
{
    _network_stop();
    ESP_ERROR_CHECK(esp_unregister_shutdown_handler(&_network_stop));

    return ESP_OK;
}

esp_ip4_addr_t network_get_ip()
{
	return s_ip_addr;
}

esp_ip4_addr_t network_get_gateway()
{
	return s_gateway_addr;
}

/*
*
*/

#ifdef CONFIG_EXAMPLE_CONNECT_WIFI

static uint16_t wifi_ap_connect_count = 0;

static void on_wifi_ap(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
	if(event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_ap_connect_count++;
        ESP_LOGI(TAG,"%d. station connected", wifi_ap_connect_count);
    } else if(event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_ap_connect_count--;
        ESP_LOGI(TAG,"station disconnected - %d remain", wifi_ap_connect_count);
    }
}

/*
*
*/

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    wifi_mode_t mode;

    switch (event_id) {
/*
    case WIFI_EVENT_STA_START:
        break;
    case WIFI_EVENT_STA_CONNECTED:
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        break;
*/
    case WIFI_EVENT_AP_START:
        esp_wifi_get_mode(&mode);

        /* TODO: get config or information of softap, then set to report extra_info */
        if (s_ble_is_connected == true) {
            if (s_wifi_status == WIFI_CONNECTED) {
                esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, NULL);
            } else {
                esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, NULL);
            }
        } else {
            BLUFI_INFO("BLUFI BLE is not connected yet\n");
        }
        break;
    case WIFI_EVENT_SCAN_DONE: {
        uint16_t apCount = 0;
        esp_wifi_scan_get_ap_num(&apCount);
        if (apCount == 0) {
            BLUFI_INFO("Nothing AP found");
            break;
        }
        wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * apCount);
        if (!ap_list) {
            BLUFI_ERROR("malloc error, ap_list is NULL");
            break;
        }
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&apCount, ap_list));
        esp_blufi_ap_record_t * blufi_ap_list = (esp_blufi_ap_record_t *)malloc(apCount * sizeof(esp_blufi_ap_record_t));
        if (!blufi_ap_list) {
            if (ap_list) {
                free(ap_list);
            }
            BLUFI_ERROR("malloc error, blufi_ap_list is NULL");
            break;
        }
        for (int i = 0; i < apCount; ++i)
        {
            blufi_ap_list[i].rssi = ap_list[i].rssi;
            memcpy(blufi_ap_list[i].ssid, ap_list[i].ssid, sizeof(ap_list[i].ssid));
        }

        if (s_ble_is_connected == true) {
            esp_blufi_send_wifi_list(apCount, blufi_ap_list);
        } else {
            BLUFI_INFO("BLUFI BLE is not connected yet\n");
        }

        esp_wifi_scan_stop();
        free(ap_list);
        free(blufi_ap_list);
        break;
    }
    default:
        break;
    }
    return;
}

static void on_wifi_start(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
	s_wifi_status = WIFI_STARTED;

    esp_err_t err = esp_wifi_connect();

    led_normal_flash(&s_led25);

    switch(err) {
		case ESP_OK: s_wifi_status = WIFI_CONNECTING;
			break;
		case ESP_ERR_WIFI_CONN: s_wifi_status = WIFI_INTERNAL_ERROR;
			break;
		default:	s_wifi_status = WIFI_CONNECT_FAIL;
			break;
	}
    ESP_ERROR_CHECK(err);
}

static void on_wifi_disconnect(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Wi-Fi disconnected, trying to reconnect...");

    s_ip_addr.addr = 0;
    s_wifi_status = WIFI_DISCONNECTED;

    led_off(&s_led25);

    esp_err_t err = esp_wifi_connect();
    switch(err) {
		case ESP_OK: s_wifi_status = WIFI_CONNECTING;
			break;
		case ESP_ERR_WIFI_CONN: s_wifi_status = WIFI_INTERNAL_ERROR;
			break;
		default:	s_wifi_status = WIFI_CONNECT_FAIL;
			break;
	}
    ESP_ERROR_CHECK(err);
}

static void on_wifi_connect(void *esp_netif, esp_event_base_t event_base,
                            int32_t event_id, void *event_data)
{
    s_wifi_status = WIFI_CONNECTING;

#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
    esp_netif_create_ip6_linklocal(esp_netif);
#endif // CONFIG_EXAMPLE_CONNECT_IPV6

    wifi_event_sta_connected_t *event;
	event = (wifi_event_sta_connected_t*) event_data;
	memcpy(s_conn_bssid, event->bssid, 6);
	memcpy(s_conn_ssid, event->ssid, event->ssid_len);
	s_conn_ssid_len = event->ssid_len;
}

/*
*
*/

#define CMD_SSID  "ssid"
#define CMD_PASS  "pass"
#define CMD_BSSID  "bssid"
#define CMD_CHANNEL  "channel"
#define CMD_WIFI_STATIC_IP "static_ip"
#define CMD_WIFI_STATIC_GATEWAY "static_gateway"
#define CMD_WIFI_STATIC_NETMASK "ststic_netmask"

#define CMD_AP_MAC  "ap_mac"
#define CMD_AP_SSID  "ap_ssid"
#define CMD_AP_PASS  "ap_pass"
#define CMD_AP_CHANNEL  "ap_channel"

static nvs_handle my_nvs_handle;

#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASS_MAX_LEN 32

static uint8_t s_ssid[WIFI_SSID_MAX_LEN] = {0};
static uint8_t s_pass[WIFI_PASS_MAX_LEN] = {0};
static uint8_t s_bssid[6] = {0};
static uint8_t s_channel = 0;
static esp_ip4_addr_t s_wifi_static_ip4_addr = {0};
static esp_ip4_addr_t s_wifi_static_gateway_ip4_addr = {0};
static esp_ip4_addr_t s_wifi_static_netmask_ip4_addr = {0};

static uint8_t s_ap_mac[6] = {0};
static uint8_t s_ap_ssid[WIFI_SSID_MAX_LEN] = {0};
static uint8_t s_ap_pass[WIFI_PASS_MAX_LEN] = {0};
static uint8_t s_ap_channel = 0;

static bool is_null_mac(uint8_t *mac)
{
	if(mac[0] == 0 && mac[1] == 0 && mac[2] == 0 && mac[3] == 0 && mac[4] == 0 && mac[5] == 0)
		return true;
	else
		return false;
}

void wifi_load_config()
{
	esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_nvs_handle);
	if(err != ESP_OK) {
		ESP_LOGE(TAG, "Error (%d) opening NVS!\n", err);
		return;
	}

	size_t l = WIFI_SSID_MAX_LEN;
	err = nvs_get_str (my_nvs_handle, CMD_SSID, (char*)s_ssid, &l);
	if(err != ESP_OK) {
		ESP_LOGE(TAG, "No SSID cached ...");
	} else
		ESP_LOGI(TAG, "SSID: %s", s_ssid);

	l = WIFI_PASS_MAX_LEN;
	err = nvs_get_str (my_nvs_handle, CMD_PASS, (char*)s_pass, &l);
	if(err != ESP_OK) {
		ESP_LOGE(TAG, "No Password cached ...");
  	} else
		ESP_LOGI(TAG, "Password: %s", s_pass);

	l = 6; /* MAC Address */
	err = nvs_get_blob(my_nvs_handle, CMD_BSSID, &s_bssid, &l);
	if(err != ESP_OK) {
		ESP_LOGE(TAG, "No BSSID cached ...");
	} else {
		ESP_LOGI(TAG, "BSSID: %02x:%02x:%02x:%02x:%02x:%02x", s_bssid[0], s_bssid[1], s_bssid[2], s_bssid[3], s_bssid[4], s_bssid[5]);
	}

	err = nvs_get_u8 (my_nvs_handle, CMD_CHANNEL, &s_channel);
	if(err != ESP_OK) {
		ESP_LOGE(TAG, "No Channel cached ...");
  	} else
		ESP_LOGI(TAG, "Channel: %d", s_channel);

	memset(&s_wifi_static_ip4_addr, 0, sizeof(s_wifi_static_ip4_addr));
	l = sizeof(s_wifi_static_ip4_addr);
	err = nvs_get_blob(my_nvs_handle, CMD_WIFI_STATIC_IP, &s_wifi_static_ip4_addr, &l);
	if(err != ESP_OK) {
		ESP_LOGE(TAG, "No Static IP cached ...");
	} else {
		char buf[16];
		ESP_LOGI(TAG, "Static IP: %s", esp_ip4addr_ntoa(&s_wifi_static_ip4_addr, buf, 16));
	}

	memset(&s_wifi_static_gateway_ip4_addr, 0, sizeof(s_wifi_static_gateway_ip4_addr));
	l = sizeof(s_wifi_static_gateway_ip4_addr);
	err = nvs_get_blob(my_nvs_handle, CMD_WIFI_STATIC_GATEWAY, &s_wifi_static_gateway_ip4_addr, &l);
	if(err != ESP_OK) {
		ESP_LOGE(TAG, "No Gateway cached ...");
	} else {
		char buf[16];
		ESP_LOGI(TAG, "Gateway: %s", esp_ip4addr_ntoa(&s_wifi_static_gateway_ip4_addr, buf, 16));
	}

	memset(&s_wifi_static_netmask_ip4_addr, 0, sizeof(s_wifi_static_netmask_ip4_addr));
	l = sizeof(s_wifi_static_netmask_ip4_addr);
	err = nvs_get_blob(my_nvs_handle, CMD_WIFI_STATIC_NETMASK, &s_wifi_static_netmask_ip4_addr, &l);
	if(err != ESP_OK) {
		ESP_LOGE(TAG, "No Netmask cached ...");
	} else {
		char buf[16];
		ESP_LOGI(TAG, "Netmask: %s", esp_ip4addr_ntoa(&s_wifi_static_netmask_ip4_addr, buf, 16));
	}

	l = 6; /* MAC Address */
	err = nvs_get_blob(my_nvs_handle, CMD_AP_MAC, &s_ap_mac, &l);
	if(err != ESP_OK) {
		ESP_LOGE(TAG, "No AP MAC cached ...");
	} else {
		ESP_LOGI(TAG, "AP MAC: %02x:%02x:%02x:%02x:%02x:%02x", s_ap_mac[0], s_ap_mac[1], s_ap_mac[2], s_ap_mac[3], s_ap_mac[4], s_ap_mac[5]);
	}

	l = WIFI_SSID_MAX_LEN;
	err = nvs_get_str (my_nvs_handle, CMD_AP_SSID, (char*)s_ap_ssid, &l);
	if(err != ESP_OK) {
		ESP_LOGE(TAG, "No AP SSID cached ...");
	} else
		ESP_LOGI(TAG, "AP SSID: %s", s_ap_ssid);

	l = WIFI_PASS_MAX_LEN;
	err = nvs_get_str (my_nvs_handle, CMD_AP_PASS, (char*)s_ap_pass, &l);
	if(err != ESP_OK) {
		ESP_LOGE(TAG, "No AP Password cached ...");
  	} else
		ESP_LOGI(TAG, "AP Password: %s", s_ap_pass);

	err = nvs_get_u8 (my_nvs_handle, CMD_AP_CHANNEL, &s_ap_channel);
	if(err != ESP_OK) {
		ESP_LOGE(TAG, "No AP Channel cached ...");
  	} else
		ESP_LOGI(TAG, "AP Channel: %d", s_ap_channel);

	nvs_close(my_nvs_handle);
}

void wifi_save_config()
{
	esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_nvs_handle);
	if(err != ESP_OK) {
		ESP_LOGE(TAG, "Error (%d) opening NVS!\n", err);
		return;
	}

	if(strlen((char*)s_ssid) > 0) {
		err = nvs_set_str(my_nvs_handle, CMD_SSID, (char*)s_ssid);
		if(err != ESP_OK)
			ESP_LOGE(TAG, "Fail Save SSID !!!");
	} else
		nvs_erase_key(my_nvs_handle, CMD_SSID);

	if(strlen((char*)s_pass) > 0) {
		err = nvs_set_str(my_nvs_handle, CMD_PASS, (char*)s_pass);
		if(err != ESP_OK)
			ESP_LOGE(TAG, "Fail Save Password !!!");
	} else
		nvs_erase_key(my_nvs_handle, CMD_PASS);

	//if(s_bssid[0] != 0 || s_bssid[1] != 0 || s_bssid[2] != 0 || s_bssid[3] != 0 || s_bssid[4] != 0 || s_bssid[5] != 0) {
	if(is_null_mac(s_bssid) == false) {
		err = nvs_set_blob(my_nvs_handle, CMD_BSSID, &s_bssid, 6);
		if(err != ESP_OK)
			ESP_LOGE(TAG, "Fail Save BSSID !!!");
	} else
		nvs_erase_key(my_nvs_handle, CMD_BSSID);

	if(s_channel > 0) {
		err = nvs_set_u8(my_nvs_handle, CMD_CHANNEL, s_channel);
		if(err != ESP_OK)
			ESP_LOGE(TAG, "Fail Save Channel !!!");	
	} else
		nvs_erase_key(my_nvs_handle, CMD_CHANNEL);

	if(s_wifi_static_ip4_addr.addr != 0) {
		err = nvs_set_blob(my_nvs_handle, CMD_WIFI_STATIC_IP, &s_wifi_static_ip4_addr, sizeof(esp_ip4_addr_t));
		if(err != ESP_OK)
			ESP_LOGE(TAG, "Fail Save Static IP !!!");
	} else
		nvs_erase_key(my_nvs_handle, CMD_WIFI_STATIC_IP);

	if(s_wifi_static_gateway_ip4_addr.addr != 0) {
		err = nvs_set_blob(my_nvs_handle, CMD_WIFI_STATIC_GATEWAY, &s_wifi_static_gateway_ip4_addr, sizeof(esp_ip4_addr_t));
		if(err != ESP_OK)
			ESP_LOGE(TAG, "Fail Save Gateway !!!");
	} else
		nvs_erase_key(my_nvs_handle, CMD_WIFI_STATIC_GATEWAY);

	if(s_wifi_static_netmask_ip4_addr.addr != 0) {
		err = nvs_set_blob(my_nvs_handle, CMD_WIFI_STATIC_NETMASK, &s_wifi_static_netmask_ip4_addr, sizeof(esp_ip4_addr_t));
		if(err != ESP_OK)
			ESP_LOGE(TAG, "Fail Save Netmask !!!");
	} else
		nvs_erase_key(my_nvs_handle, CMD_WIFI_STATIC_NETMASK);

	//if(s_ap_mac[0] != 0 || s_ap_mac[1] != 0 || s_ap_mac[2] != 0 || s_ap_mac[3] != 0 || s_ap_mac[4] != 0 || s_ap_mac[5] != 0) {
	if(is_null_mac(s_ap_mac) == false) {
		err = nvs_set_blob(my_nvs_handle, CMD_AP_MAC, &s_ap_mac, 6);
		if(err != ESP_OK)
			ESP_LOGE(TAG, "Fail Save AP MAC !!!");
	} else
		nvs_erase_key(my_nvs_handle, CMD_AP_MAC);

	if(strlen((char*)s_ap_ssid) > 0) {
		err = nvs_set_str(my_nvs_handle, CMD_AP_SSID, (char*)s_ap_ssid);
		if(err != ESP_OK)
			ESP_LOGE(TAG, "Fail Save AP SSID !!!");
	} else
		nvs_erase_key(my_nvs_handle, CMD_AP_SSID);

	if(strlen((char*)s_ap_pass) > 0) {
		err = nvs_set_str(my_nvs_handle, CMD_AP_PASS, (char*)s_ap_pass);
		if(err != ESP_OK)
			ESP_LOGE(TAG, "Fail Save AP Password !!!");
	} else
		nvs_erase_key(my_nvs_handle, CMD_AP_PASS);

	if(s_ap_channel > 0) {
		err = nvs_set_u8(my_nvs_handle, CMD_AP_CHANNEL, s_ap_channel);
		if(err != ESP_OK)
			ESP_LOGE(TAG, "Fail Save AP Channel !!!");	
	} else
		nvs_erase_key(my_nvs_handle, CMD_AP_CHANNEL);

	nvs_commit(my_nvs_handle);
	nvs_close(my_nvs_handle);
}

void wifi_factory_reset()
{
	esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_nvs_handle);

	if(err != ESP_OK) {
		ESP_LOGE(TAG, "Error (%d) opening NVS!\n", err);
		return;
	}

	nvs_erase_key(my_nvs_handle, CMD_SSID);
	nvs_erase_key(my_nvs_handle, CMD_PASS);
	nvs_erase_key(my_nvs_handle, CMD_BSSID);
	nvs_erase_key(my_nvs_handle, CMD_CHANNEL);
	nvs_erase_key(my_nvs_handle, CMD_WIFI_STATIC_IP);
	nvs_erase_key(my_nvs_handle, CMD_WIFI_STATIC_GATEWAY);
	nvs_erase_key(my_nvs_handle, CMD_WIFI_STATIC_NETMASK);

	nvs_erase_key(my_nvs_handle, CMD_AP_MAC);
	nvs_erase_key(my_nvs_handle, CMD_AP_SSID);
	nvs_erase_key(my_nvs_handle, CMD_AP_PASS);
	nvs_erase_key(my_nvs_handle, CMD_AP_CHANNEL);

	nvs_commit(my_nvs_handle);
	nvs_close(my_nvs_handle);
}

void wifi_scan_start(char *ssid, uint8_t *bssid, uint8_t channel)
{
	wifi_scan_config_t scan_config = {};
	scan_config.ssid = (uint8_t *)ssid;
	scan_config.bssid = bssid;
	scan_config.channel = channel;
	scan_config.show_hidden = 1;

	esp_wifi_scan_start(&scan_config, 0);
}

void wifi_set_ssid(const char *ssid)
{
	if(ssid == 0 || strlen(ssid) == 0) {
		memset(s_ssid, 0, WIFI_SSID_MAX_LEN);
		return;
	}

	snprintf((char *)s_ssid, WIFI_SSID_MAX_LEN, "%s", ssid);
}

const char *wifi_get_ssid()
{
	if(strlen((char*)s_ssid) > 0)
		return (const char *)s_ssid;
	return CONFIG_EXAMPLE_WIFI_SSID;
}

void wifi_set_password(const char *pass)
{
	if(pass == 0 || strlen(pass) == 0) {
		memset(s_pass, 0, WIFI_PASS_MAX_LEN);
		return;
	}

	snprintf((char *)s_pass, WIFI_PASS_MAX_LEN, "%s", pass);
}

const char *wifi_get_password()
{
	if(strlen((char*)s_pass) > 0)
		return (const char *)s_pass;

	return CONFIG_EXAMPLE_WIFI_PASSWORD;
}

void wifi_set_bssid(uint8_t *bssid)
{
	if(bssid)
		memcpy(&s_bssid[0], bssid, 6);
}

uint8_t *wifi_get_bssid()
{
	return &s_bssid[0];
}

void wifi_set_channel(uint8_t ch)
{
	s_channel = ch;
}

uint8_t wifi_get_channel()
{
	return s_channel;
}

void wifi_set_ap_mac(uint8_t *mac)
{
	if(mac)
		memcpy(&s_ap_mac[0], mac, 6);
}

uint8_t *wifi_get_ap_mac()
{
	static uint8_t mac[6] = {0}; 
	//if(s_ap_mac[0] == 0 && s_ap_mac[1] == 0 && s_ap_mac[2] == 0 && s_ap_mac[3] == 0 && s_ap_mac[4] == 0 && s_ap_mac[5] == 0) {
	if(is_null_mac(s_ap_mac)) {
		esp_wifi_get_mac(WIFI_IF_AP, mac);
		return &mac[0];
	} else
		return &s_ap_mac[0];
}

void wifi_set_ap_ssid(const char *ssid)
{
	if(ssid == 0 || strlen(ssid) == 0) {
		memset(s_ap_ssid, 0, WIFI_SSID_MAX_LEN);
		return;
	}

	snprintf((char *)s_ap_ssid, WIFI_SSID_MAX_LEN, "%s", ssid);
}

const char *wifi_get_ap_ssid()
{
	if(strlen((char*)s_ap_ssid) > 0)
		return (const char *)s_ap_ssid;
	return CONFIG_EXAMPLE_WIFI_AP_SSID;
}

void wifi_set_ap_password(const char *pass)
{
	if(pass == 0 || strlen(pass) == 0) {
		memset(s_ap_pass, 0, WIFI_PASS_MAX_LEN);
		return;
	}

	snprintf((char *)s_ap_pass, WIFI_PASS_MAX_LEN, "%s", pass);
}

const char *wifi_get_ap_password()
{
	if(strlen((char*)s_ap_pass) > 0)
		return (const char *)s_ap_pass;

	return CONFIG_EXAMPLE_WIFI_AP_PASSWORD;
}

void wifi_set_ap_channel(uint8_t ch)
{
	s_ap_channel = ch;
}

uint8_t wifi_get_ap_channel()
{
	return s_ap_channel;
}

void wifi_set_static_ip(esp_ip4_addr_t *ip4)
{
	if(ip4 == 0 || ip4->addr == 0)
		s_wifi_static_ip4_addr.addr = 0;
	else
		s_wifi_static_ip4_addr = *ip4;
}

esp_ip4_addr_t wifi_get_static_ip()
{
	return s_wifi_static_ip4_addr;
}

void wifi_set_static_gateway(esp_ip4_addr_t *ip4)
{
	if(ip4 == 0 || ip4->addr == 0)
		s_wifi_static_gateway_ip4_addr.addr = 0;
	else
		s_wifi_static_gateway_ip4_addr = *ip4;
}

esp_ip4_addr_t wifi_get_static_gateway()
{
	return s_wifi_static_gateway_ip4_addr;
}

void wifi_set_static_netmask(esp_ip4_addr_t *ip4)
{
	if(ip4 == 0 || ip4->addr == 0)
		s_wifi_static_netmask_ip4_addr.addr = 0;
	else
		s_wifi_static_netmask_ip4_addr = *ip4;
}

esp_ip4_addr_t wifi_get_static_netmask()
{
	return s_wifi_static_netmask_ip4_addr;
}

wifi_status_t wifi_get_status()
{
	return s_wifi_status;
}

/*
*
*/

static esp_netif_t *wifi_sta_config(wifi_config_t *wifi_config)
{
#if 0
    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    // Prefix the interface description with the module TAG
    // Warning: the interface desc is used in tests to capture actual connection details (IP, gw, mask)
    char *desc;
    asprintf(&desc, "%s: %s", TAG, esp_netif_config.if_desc);
    esp_netif_config.if_desc = desc;
    esp_netif_config.route_prio = 128;
    s_netif_sta = esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
    free(desc);
    esp_wifi_set_default_wifi_sta_handlers();
#else
	s_netif_sta = esp_netif_create_default_wifi_sta();
#endif
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_START, &on_wifi_start, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &on_wifi_connect, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL, NULL));
#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6, NULL, NULL));
#endif

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

	if(s_wifi_static_ip4_addr.addr != 0) {
		esp_netif_ip_info_t ip_info;
		esp_netif_dns_info_t dns_info; 
		
		esp_netif_dhcpc_stop(s_netif_sta);
		memset(&ip_info, 0, sizeof(ip_info));
		ip_info.ip = s_wifi_static_ip4_addr;
		ip_info.gw = s_wifi_static_gateway_ip4_addr;
		if(s_wifi_static_netmask_ip4_addr.addr == 0) {
			esp_ip4_addr_t nm_addr;
			esp_netif_set_ip4_addr(&nm_addr, 255, 255, 255, 0);
			ip_info.netmask = nm_addr;
		} else
			ip_info.netmask = s_wifi_static_netmask_ip4_addr;

		ip_addr_t dns_addr;
		inet_pton(AF_INET, "8.8.8.8", &dns_addr);
		dns_setserver(1, (const ip_addr_t *)&dns_addr);
		dns_info.ip.u_addr.ip4.addr = dns_addr.u_addr.ip4.addr;

		esp_netif_set_ip_info(s_netif_sta, &ip_info);
		ESP_LOGI(TAG, "IP : " IPSTR, IP2STR(&ip_info.ip));
		ESP_LOGI(TAG, "GATEWAY : " IPSTR, IP2STR(&ip_info.gw));
		esp_netif_set_dns_info(s_netif_sta, ESP_NETIF_DNS_MAIN, &dns_info);

		const ip_addr_t *dns_ip = dns_getserver(IPADDR_TYPE_V4);
		ESP_LOGI(TAG, "DNS : " IPSTR, IP2STR(&dns_ip->u_addr.ip4));
	}

	if(s_ssid[0] != '\0')
		memcpy(wifi_config->sta.ssid, s_ssid, WIFI_SSID_MAX_LEN);
	if(s_pass[0] != '\0')
		memcpy(wifi_config->sta.password, s_pass, WIFI_PASS_MAX_LEN);
	//if(s_bssid[0] != 0 || s_bssid[1] != 0 || s_bssid[2] != 0 || s_bssid[3] != 0 || s_bssid[4] != 0 || s_bssid[5] != 0) {
	if(is_null_mac(s_bssid) == false) {
		ESP_LOGE(TAG, "Connect to BSSID: %02x:%02x:%02x:%02x:%02x:%02x", s_bssid[0], s_bssid[1], s_bssid[2], s_bssid[3], s_bssid[4], s_bssid[5]);
		memcpy(wifi_config->sta.bssid, s_bssid, 6);
		wifi_config->sta.bssid_set = 1;
	}
	wifi_config->sta.channel = s_channel;

    ESP_LOGI(TAG, "Connect to SSID : %s password : %s channel : %d",
             wifi_config->sta.ssid, wifi_config->sta.password, wifi_config->sta.channel);

	return s_netif_sta;
}

#ifdef CONFIG_EXAMPLE_ENABLE_WIFI_AP

static esp_netif_t *wifi_ap_config(wifi_config_t *ap_config)
{
	//if(s_ap_mac[0] != 0 || s_ap_mac[1] != 0 || s_ap_mac[2] != 0 || s_ap_mac[3] != 0 || s_ap_mac[4] != 0 || s_ap_mac[5] != 0)
	if(is_null_mac(s_ap_mac) == false) {
		esp_wifi_set_mac(WIFI_IF_AP, s_ap_mac);
	}
#if 0
    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_AP();
    // Prefix the interface description with the module TAG
    // Warning: the interface desc is used in tests to capture actual connection details (IP, gw, mask)
    char *desc;
    asprintf(&desc, "%s: %s", TAG, esp_netif_config.if_desc);
    esp_netif_config.if_desc = desc;
    //esp_netif_config.route_prio = 128;
    s_netif_ap = esp_netif_create_wifi(WIFI_IF_AP, &esp_netif_config);
    free(desc);
    esp_wifi_set_default_wifi_ap_handlers();
#else
	s_netif_ap = esp_netif_create_default_wifi_ap();
#endif
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &on_wifi_ap, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &on_wifi_ap, NULL, NULL));

	esp_netif_ip_info_t ipInfo_ap;
#if CONFIG_EXAMPLE_WIFI_AP_DAISY_CHAIN
	IP4_ADDR(&ipInfo_ap.ip, 172,16,s_ap_mac[2],1);
	IP4_ADDR(&ipInfo_ap.gw, 172,16,s_ap_mac[2],1);
#else
	IP4_ADDR(&ipInfo_ap.ip, 172,16,4,1);
	IP4_ADDR(&ipInfo_ap.gw, 172,16,4,1);
#endif
	IP4_ADDR(&ipInfo_ap.netmask, 255,255,255,0);

	esp_netif_dhcps_stop(s_netif_ap); // stop before setting ip WifiAP
	esp_netif_set_ip_info(s_netif_ap, &ipInfo_ap);
	esp_netif_dhcps_start(s_netif_ap);

    if (strlen(CONFIG_EXAMPLE_WIFI_AP_PASSWORD) < 8) {
        ap_config->ap.authmode = WIFI_AUTH_OPEN;
    }

	if(s_ap_ssid[0] != '\0')
		memcpy(ap_config->ap.ssid, s_ap_ssid, WIFI_SSID_MAX_LEN);
	if(s_ap_pass[0] != '\0')
		memcpy(ap_config->ap.password, s_ap_pass, WIFI_PASS_MAX_LEN);
	if(s_ap_channel != 0)
		ap_config->ap.channel = s_ap_channel;

    ESP_LOGI(TAG, "wifi_init_softap SSID : %s password : %s channel : %d",
             ap_config->ap.ssid, ap_config->ap.password, ap_config->ap.channel);

    // Enable DNS (offer) for dhcp server
    dhcps_offer_t dhcps_dns_value = OFFER_DNS;
    dhcps_set_option_info(6, &dhcps_dns_value, sizeof(dhcps_dns_value));

#define MY_DNS_IP_ADDR 0x08080808 // 8.8.8.8

    ip_addr_t dnsserver;
    // Set custom dns server address for dhcp server
    dnsserver.u_addr.ip4.addr = htonl(MY_DNS_IP_ADDR);
    dnsserver.type = IPADDR_TYPE_V4;
    dhcps_dns_setserver(&dnsserver);

    return s_netif_ap;
}

#endif // CONFIG_EXAMPLE_ENABLE_WIFI_AP

static esp_netif_t *wifi_start(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
#ifdef CONFIG_EXAMPLE_ENABLE_WIFI_AP
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
	wifi_config_t ap_config = {
        .ap = {
        	.ssid = CONFIG_EXAMPLE_WIFI_AP_SSID,
        	.ssid_len = strlen(CONFIG_EXAMPLE_WIFI_AP_SSID),
        	.password = CONFIG_EXAMPLE_WIFI_AP_PASSWORD,
            .channel = 0,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .ssid_hidden = 0,
            .max_connection = 8,
            .beacon_interval = 100,
        }
    };
    wifi_ap_config(&ap_config);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
#else
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
#endif
    wifi_config_t sta_config = {
        .sta = {
            .ssid = CONFIG_EXAMPLE_WIFI_SSID,
            .password = CONFIG_EXAMPLE_WIFI_PASSWORD,
            .scan_method = EXAMPLE_WIFI_SCAN_METHOD,
            .sort_method = EXAMPLE_WIFI_CONNECT_AP_SORT_METHOD,
            .threshold.rssi = CONFIG_EXAMPLE_WIFI_SCAN_RSSI_THRESHOLD,
            .threshold.authmode = EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD,
        },
    };

	wifi_sta_config(&sta_config);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    if(s_enable_blufi == false) /* Button released ... Blufi disabled */
    	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    else /* Blufi enabled */
    	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));

    /*
        78: 19.5dBm
        76: 19dBm
        74: 18.5dBm
        68: 17dBm
        60: 15dBm
        52: 13dBm
        44: 11dBm
        34: 8.5dBm
        28: 7dBm
        20: 5dBm
        8: 2dBm
        -4: -1dBm
    */
    esp_wifi_set_max_tx_power(78);

    int8_t tx_power = 0;
    esp_wifi_get_max_tx_power(&tx_power);
    ESP_LOGI(TAG, "maximum tx power is %.2f dBm", tx_power * 0.25); 

#if defined(CONFIG_EXAMPLE_ENABLE_WIFI_AP) && defined(CONFIG_LWIP_IP_FORWARD)
    esp_netif_ip_info_t ipInfo_ap;
    esp_netif_set_ip_info(s_netif_ap, &ipInfo_ap);
    ip_napt_enable(ipInfo_ap.ip.addr, 1);
    ESP_LOGI(TAG, "NAT is enabled");
#endif

#ifdef CONFIG_EXAMPLE_ENABLE_WIFI_AP
    return s_netif_ap;
#else
    return s_netif_sta;
#endif
}

static void wifi_stop(void)
{
	if(s_wifi_status == WIFI_STOPPED)
		return;

    //esp_netif_t *wifi_netif = get_network_netif_from_desc("sta");
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_START, &on_wifi_start));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &on_wifi_connect));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip));
#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6));
#endif
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(s_netif_sta));
    esp_netif_destroy(s_netif_sta);
#ifdef CONFIG_EXAMPLE_ENABLE_WIFI_AP
    ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(s_netif_ap));
    esp_netif_destroy(s_netif_ap);
    s_netif_ap = NULL;
#endif
    s_netif_sta = NULL;
    s_network_esp_netif = NULL;

	s_wifi_status = WIFI_STOPPED;
}

#endif // CONFIG_EXAMPLE_CONNECT_WIFI

/*
*
*/

#define CMD_ETH_STATIC_IP "eth_static_ip"
#define CMD_ETH_GATEWAY "eth_gateway"

static esp_ip4_addr_t s_eth_static_ip4_addr;
static esp_ip4_addr_t s_eth_gateway_ip4_addr;

void eth_load_config()
{
	esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_nvs_handle);
	if(err != ESP_OK) {
		ESP_LOGE(TAG, "Error (%d) opening NVS!\n", err);
		return;
	}

	memset(&s_eth_static_ip4_addr, 0, sizeof(s_eth_static_ip4_addr));
	size_t l = sizeof(s_eth_static_ip4_addr);
	err = nvs_get_blob(my_nvs_handle, CMD_ETH_STATIC_IP, &s_eth_static_ip4_addr, &l);
	if(err != ESP_OK) {
		ESP_LOGE(TAG, "No Static IP cached ...");
	} else {
		char buf[16];
		ESP_LOGI(TAG, "Static IP: %s", esp_ip4addr_ntoa(&s_eth_static_ip4_addr, buf, 16));
	}

	memset(&s_eth_gateway_ip4_addr, 0, sizeof(s_eth_gateway_ip4_addr));
	l = sizeof(s_eth_gateway_ip4_addr);
	err = nvs_get_blob(my_nvs_handle, CMD_ETH_GATEWAY, &s_eth_gateway_ip4_addr, &l);
	if(err != ESP_OK) {
		ESP_LOGE(TAG, "No Gateway cached ...");
	} else {
		char buf[16];
		ESP_LOGI(TAG, "Gateway: %s", esp_ip4addr_ntoa(&s_eth_gateway_ip4_addr, buf, 16));
	}

	nvs_close(my_nvs_handle);
}

void eth_save_config()
{
	esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_nvs_handle);
	if(err != ESP_OK) {
		ESP_LOGE(TAG, "Error (%d) opening NVS!\n", err);
		return;
	}

	if(s_eth_static_ip4_addr.addr != 0) {
		err = nvs_set_blob(my_nvs_handle, CMD_ETH_STATIC_IP, &s_eth_static_ip4_addr, sizeof(esp_ip4_addr_t));
		if(err != ESP_OK)
			ESP_LOGE(TAG, "Fail Save Static IP !!!");
	} else
		nvs_erase_key(my_nvs_handle, CMD_ETH_STATIC_IP);

	if(s_eth_gateway_ip4_addr.addr != 0) {
		err = nvs_set_blob(my_nvs_handle, CMD_ETH_GATEWAY, &s_eth_gateway_ip4_addr, sizeof(esp_ip4_addr_t));
		if(err != ESP_OK)
			ESP_LOGE(TAG, "Fail Save Gateway !!!");
	} else
		nvs_erase_key(my_nvs_handle, CMD_ETH_GATEWAY);

	nvs_commit(my_nvs_handle);
	nvs_close(my_nvs_handle);
}

void eth_factory_reset()
{
	esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_nvs_handle);

	if(err != ESP_OK) {
		ESP_LOGE(TAG, "Error (%d) opening NVS!\n", err);
		return;
	}

	nvs_erase_key(my_nvs_handle, CMD_ETH_STATIC_IP);
	nvs_erase_key(my_nvs_handle, CMD_ETH_GATEWAY);

	nvs_commit(my_nvs_handle);
	nvs_close(my_nvs_handle);
}

void eth_set_static_ip(esp_ip4_addr_t *ip4)
{
	if(ip4 == 0 || ip4->addr == 0)
		s_eth_static_ip4_addr.addr = 0;
	else
		s_eth_static_ip4_addr = *ip4;
}

esp_ip4_addr_t eth_get_static_ip()
{
	return s_eth_static_ip4_addr;
}

void eth_set_static_gateway(esp_ip4_addr_t *ip4)
{
	if(ip4 == 0 || ip4->addr == 0)
		s_eth_gateway_ip4_addr.addr = 0;
	else
		s_eth_gateway_ip4_addr = *ip4;
}

esp_ip4_addr_t eth_get_static_gateway()
{
	return s_eth_gateway_ip4_addr;
}

#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET

/** Event handler for Ethernet events */
static void on_eth_event(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;
	
    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_ERROR_CHECK(esp_netif_create_ip6_linklocal(arg));
#else
        s_eth_status = ETH_CONNECTING;
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
#endif
        break;
    case ETHERNET_EVENT_DISCONNECTED:
    	s_eth_status = ETH_DISCONNECTED;
        ESP_LOGI(TAG, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
    	s_eth_status = ETH_STARTED;
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
    	s_eth_status = ETH_STOPPED;
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

static esp_eth_handle_t s_eth_handle = NULL;
static esp_eth_mac_t *s_mac = NULL;
static esp_eth_phy_t *s_phy = NULL;
static esp_eth_netif_glue_handle_t s_eth_glue = NULL;

static esp_netif_t *eth_start(void)
{
    char *desc;
    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    // Prefix the interface description with the module TAG
    // Warning: the interface desc is used in tests to capture actual connection details (IP, gw, mask)
    asprintf(&desc, "%s: %s", TAG, esp_netif_config.if_desc);
    esp_netif_config.if_desc = desc;
    esp_netif_config.route_prio = 64;
    esp_netif_config_t netif_config = {
        .base = &esp_netif_config,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
    };
    s_netif_eth = esp_netif_new(&netif_config);
    assert(s_netif_eth);
    free(desc);

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = CONFIG_EXAMPLE_ETH_PHY_ADDR;
    phy_config.reset_gpio_num = CONFIG_EXAMPLE_ETH_PHY_RST_GPIO;
#if CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET
    mac_config.smi_mdc_gpio_num = CONFIG_EXAMPLE_ETH_MDC_GPIO;
    mac_config.smi_mdio_gpio_num = CONFIG_EXAMPLE_ETH_MDIO_GPIO;
    s_mac = esp_eth_mac_new_esp32(&mac_config);
#if CONFIG_EXAMPLE_ETH_PHY_IP101
    s_phy = esp_eth_phy_new_ip101(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_RTL8201
    s_phy = esp_eth_phy_new_rtl8201(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_LAN87XX
    s_phy = esp_eth_phy_new_lan87xx(&phy_config);
#elif CONFIG_EXAMPLE_ETH_PHY_DP83848
    s_phy = esp_eth_phy_new_dp83848(&phy_config);
#endif
#elif CONFIG_EXAMPLE_USE_SPI_ETHERNET
    gpio_install_isr_service(0);
    spi_device_handle_t spi_handle = NULL;
    spi_bus_config_t buscfg = {
        .miso_io_num = CONFIG_EXAMPLE_ETH_SPI_MISO_GPIO,
        .mosi_io_num = CONFIG_EXAMPLE_ETH_SPI_MOSI_GPIO,
        .sclk_io_num = CONFIG_EXAMPLE_ETH_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(CONFIG_EXAMPLE_ETH_SPI_HOST, &buscfg, 1));
#if CONFIG_EXAMPLE_USE_DM9051
    spi_device_interface_config_t devcfg = {
        .command_bits = 1,
        .address_bits = 7,
        .mode = 0,
        .clock_speed_hz = CONFIG_EXAMPLE_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = CONFIG_EXAMPLE_ETH_SPI_CS_GPIO,
        .queue_size = 20
    };
    ESP_ERROR_CHECK(spi_bus_add_device(CONFIG_EXAMPLE_ETH_SPI_HOST, &devcfg, &spi_handle));
    /* dm9051 ethernet driver is based on spi driver */
    eth_dm9051_config_t dm9051_config = ETH_DM9051_DEFAULT_CONFIG(spi_handle);
    dm9051_config.int_gpio_num = CONFIG_EXAMPLE_ETH_SPI_INT_GPIO;
    s_mac = esp_eth_mac_new_dm9051(&dm9051_config, &mac_config);
    s_phy = esp_eth_phy_new_dm9051(&phy_config);
#elif CONFIG_EXAMPLE_USE_W5500
    spi_device_interface_config_t devcfg = {
        .command_bits = 16, // Actually it's the address phase in W5500 SPI frame
        .address_bits = 8,  // Actually it's the control phase in W5500 SPI frame
        .mode = 0,
        .clock_speed_hz = CONFIG_EXAMPLE_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = CONFIG_EXAMPLE_ETH_SPI_CS_GPIO,
        .queue_size = 20
    };
    ESP_ERROR_CHECK(spi_bus_add_device(CONFIG_EXAMPLE_ETH_SPI_HOST, &devcfg, &spi_handle));
    /* w5500 ethernet driver is based on spi driver */
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(spi_handle);
    w5500_config.int_gpio_num = CONFIG_EXAMPLE_ETH_SPI_INT_GPIO;
    mac_config.rx_task_stack_size = 4096;
    mac_config.rx_task_prio = 15;
    s_mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    s_phy = esp_eth_phy_new_w5500(&phy_config);
#endif
#elif CONFIG_EXAMPLE_USE_OPENETH
    phy_config.autonego_timeout_ms = 100;
    s_mac = esp_eth_mac_new_openeth(&mac_config);
    s_phy = esp_eth_phy_new_dp83848(&phy_config);
#endif

    // Install Ethernet driver
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(s_mac, s_phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &s_eth_handle));
#if !CONFIG_EXAMPLE_USE_INTERNAL_ETHERNET
    /* The SPI Ethernet module might doesn't have a burned factory MAC address, we cat to set it manually.
       02:00:00 is a Locally Administered OUI range so should not be used except when testing on a LAN under your control.
    */
#if 0
    ESP_ERROR_CHECK(esp_eth_ioctl(s_eth_handle, ETH_CMD_S_MAC_ADDR, (uint8_t[]) {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x56
    }));
#else
	uint8_t mac[6];
	ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_ETH));
	ESP_ERROR_CHECK(esp_eth_ioctl(s_eth_handle, ETH_CMD_S_MAC_ADDR, mac));
#endif
#endif
    // combine driver with netif
    s_eth_glue = esp_eth_new_netif_glue(s_eth_handle);
    esp_netif_attach(s_netif_eth, s_eth_glue);

    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, &on_eth_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_got_ip, NULL, NULL));
#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6, NULL));
#endif

	if(s_eth_static_ip4_addr.addr != 0) {
		esp_netif_ip_info_t ip_info;
		esp_netif_dns_info_t dns_info; 
		
		esp_netif_dhcpc_stop(s_netif_eth);
		memset(&ip_info, 0, sizeof(ip_info));
		ip_info.ip = s_eth_static_ip4_addr;
		ip_info.gw = s_eth_gateway_ip4_addr;
		esp_ip4_addr_t gw_addr;
		esp_netif_set_ip4_addr(&gw_addr, 255, 255, 255, 0);
		ip_info.netmask = gw_addr;

		ip_addr_t dns_addr;
		inet_pton(AF_INET, "8.8.8.8", &dns_addr);
		dns_setserver(1, (const ip_addr_t *)&dns_addr);
		dns_info.ip.u_addr.ip4.addr = dns_addr.u_addr.ip4.addr;

		esp_netif_set_ip_info(s_netif_eth, &ip_info);
		ESP_LOGI(TAG, "IP : " IPSTR, IP2STR(&ip_info.ip));
		ESP_LOGI(TAG, "GATEWAY : " IPSTR, IP2STR(&ip_info.gw));
		esp_netif_set_dns_info(s_netif_eth, ESP_NETIF_DNS_MAIN, &dns_info);

		const ip_addr_t *dns_ip = dns_getserver(IPADDR_TYPE_V4);
		ESP_LOGI(TAG, "DNS : " IPSTR, IP2STR(&dns_ip->u_addr.ip4));
	}

    esp_eth_start(s_eth_handle);
    return s_netif_eth;
}

static void eth_stop(void)
{
    //esp_netif_t *eth_netif = get_network_netif_from_desc("eth");
    ESP_ERROR_CHECK(esp_event_handler_unregister(ETH_EVENT, ETHERNET_EVENT_CONNECTED, &on_eth_event));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_got_ip));
#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6));
#endif
    ESP_ERROR_CHECK(esp_eth_stop(s_eth_handle));
    ESP_ERROR_CHECK(esp_eth_del_netif_glue(s_eth_glue));
    ESP_ERROR_CHECK(esp_eth_driver_uninstall(s_eth_handle));
    ESP_ERROR_CHECK(s_phy->del(s_phy));
    ESP_ERROR_CHECK(s_mac->del(s_mac));

    esp_netif_destroy(s_netif_eth);
    s_netif_eth = NULL;
    s_network_esp_netif = NULL;
}

#endif // CONFIG_EXAMPLE_CONNECT_ETHERNET

eth_status_t eth_get_status()
{
	return s_eth_status;
}

esp_netif_t *get_network_netif(void)
{
    return s_network_esp_netif;
}

esp_netif_t *get_network_netif_from_desc(const char *desc)
{
#if 0
    esp_netif_t *netif = NULL;
    char *expected_desc;
    asprintf(&expected_desc, "%s: %s", TAG, desc);
    while ((netif = esp_netif_next(netif)) != NULL) {
        if (strcmp(esp_netif_get_desc(netif), expected_desc) == 0) {
            free(expected_desc);
            return netif;
        }
    }
    free(expected_desc);
    return netif;
#else
	if(strcmp(desc, "sta") == 0)
		return s_netif_sta;
	else if(strcmp(desc, "ap") == 0)
		return s_netif_ap;
	else if(strcmp(desc, "eth") == 0)
		return s_netif_eth;
	else
		return 0;	
#endif
}

/*
*
*/

static void example_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param);

esp_blufi_callbacks_t example_callbacks = {
    .event_cb = example_event_callback,
    .negotiate_data_handler = blufi_dh_negotiate_data_handler,
    .encrypt_func = blufi_aes_encrypt,
    .decrypt_func = blufi_aes_decrypt,
    .checksum_func = blufi_crc_checksum,
};

static void example_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param)
{
	uint8_t cache[32];
    /* actually, should post to blufi_task handle the procedure,
     * now, as a example, we do it more simply */
    switch (event) {
    case ESP_BLUFI_EVENT_INIT_FINISH:
        BLUFI_INFO("BLUFI init finish\n");

        esp_blufi_adv_start();
        break;
    case ESP_BLUFI_EVENT_DEINIT_FINISH:
        BLUFI_INFO("BLUFI deinit finish\n");
        break;
    case ESP_BLUFI_EVENT_BLE_CONNECT:
        BLUFI_INFO("BLUFI ble connect\n");
        s_ble_is_connected = true;
        esp_blufi_adv_stop();
        blufi_security_init();
        break;
    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        BLUFI_INFO("BLUFI ble disconnect\n");
        s_ble_is_connected = false;
        blufi_security_deinit();
        esp_blufi_adv_start();
        break;
    case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
        BLUFI_INFO("BLUFI Set WIFI opmode %d\n", param->wifi_mode.op_mode);
        ESP_ERROR_CHECK( esp_wifi_set_mode(param->wifi_mode.op_mode) );
        break;
    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
        BLUFI_INFO("BLUFI requset wifi connect to AP\n");
        /* there is no wifi callback when the device has already connected to this wifi
        so disconnect wifi before connection.
        */
        if(s_wifi_status != WIFI_STOPPED)
        	wifi_stop();
        wifi_start();
        //esp_wifi_disconnect();
        //esp_wifi_connect();
        break;
    case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
        BLUFI_INFO("BLUFI requset wifi disconnect from AP\n");
        //esp_wifi_disconnect();
        wifi_stop();
        break;
    case ESP_BLUFI_EVENT_REPORT_ERROR:
        BLUFI_ERROR("BLUFI report error, error code %d\n", param->report_error.state);
        esp_blufi_send_error_info(param->report_error.state);
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_STATUS: {
    	wifi_mode_t mode;
    	esp_wifi_get_mode(&mode);
        if (wifi_get_status() == WIFI_CONNECTED) {
			esp_blufi_extra_info_t info;			
			memset(&info, 0, sizeof(esp_blufi_extra_info_t));
			memcpy(info.sta_bssid, s_conn_bssid, 6);
			info.sta_bssid_set = true;
			info.sta_ssid = s_conn_ssid;
			info.sta_ssid_len = s_conn_ssid_len;
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
        } else {
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, NULL);
        }
        BLUFI_INFO("BLUFI get wifi status from AP\n");

        break;
    }
    case ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE:
        BLUFI_INFO("blufi close a gatt connection");
        esp_blufi_disconnect();
        break;
    case ESP_BLUFI_EVENT_DEAUTHENTICATE_STA:
        /* TODO */
        break;
	case ESP_BLUFI_EVENT_RECV_STA_BSSID:
        //memcpy(sta_config.sta.bssid, param->sta_bssid.bssid, 6);
        //sta_config.sta.bssid_set = 1;
        //esp_wifi_set_config(WIFI_IF_STA, &sta_config);
		wifi_set_bssid(param->sta_bssid.bssid);
        BLUFI_INFO("Recv STA BSSID %s\n", param->sta_bssid.bssid);
        break;
	case ESP_BLUFI_EVENT_RECV_STA_SSID:
        strncpy((char *)cache, (char *)param->sta_ssid.ssid, param->sta_ssid.ssid_len);
        cache[param->sta_ssid.ssid_len] = '\0';
        //esp_wifi_set_config(WIFI_IF_STA, &sta_config);
		wifi_set_ssid((const char *)cache);
        BLUFI_INFO("Recv STA SSID %s\n", cache);
        break;
	case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
        strncpy((char *)cache, (char *)param->sta_passwd.passwd, param->sta_passwd.passwd_len);
        cache[param->sta_passwd.passwd_len] = '\0';
        //esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        BLUFI_INFO("Recv STA PASSWORD %s\n", cache);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_SSID:
        strncpy((char *)cache, (char *)param->softap_ssid.ssid, param->softap_ssid.ssid_len);
        cache[param->softap_ssid.ssid_len] = '\0';
        //ap_config.ap.ssid_len = param->softap_ssid.ssid_len;
        //esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        BLUFI_INFO("Recv SOFTAP SSID %s, ssid len %d\n", cache, param->softap_ssid.ssid_len);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_PASSWD:
        strncpy((char *)cache, (char *)param->softap_passwd.passwd, param->softap_passwd.passwd_len);
        cache[param->softap_passwd.passwd_len] = '\0';
        //esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        BLUFI_INFO("Recv SOFTAP PASSWORD %s len = %d\n", cache, param->softap_passwd.passwd_len);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_MAX_CONN_NUM:
        if (param->softap_max_conn_num.max_conn_num > 4) {
            return;
        }
        //ap_config.ap.max_connection = param->softap_max_conn_num.max_conn_num;
        //esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        //BLUFI_INFO("Recv SOFTAP MAX CONN NUM %d\n", ap_config.ap.max_connection);
        BLUFI_INFO("Recv SOFTAP MAX CONN NUM -> NOT Support\n");
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_AUTH_MODE:
        if (param->softap_auth_mode.auth_mode >= WIFI_AUTH_MAX) {
            return;
        }
        //ap_config.ap.authmode = param->softap_auth_mode.auth_mode;
        //esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        //BLUFI_INFO("Recv SOFTAP AUTH MODE %d\n", ap_config.ap.authmode);
        BLUFI_INFO("Recv SOFTAP AUTH MODE -> NOT Support\n");
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_CHANNEL:
        if (param->softap_channel.channel > 13) {
            return;
        }
        //ap_config.ap.channel = param->softap_channel.channel;
        //esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        wifi_set_ap_channel(param->softap_channel.channel);
        BLUFI_INFO("Recv SOFTAP CHANNEL %d\n", param->softap_channel.channel);
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_LIST:{
        wifi_scan_config_t scanConf = {
            .ssid = NULL,
            .bssid = NULL,
            .channel = 0,
            .show_hidden = false
        };
        esp_wifi_scan_start(&scanConf, true);
        break;
    }
    case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA:
        BLUFI_INFO("Recv Custom Data %d\n", param->custom_data.data_len);
        esp_log_buffer_hex("Custom Data", param->custom_data.data, param->custom_data.data_len);
        break;
	case ESP_BLUFI_EVENT_RECV_USERNAME:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_CA_CERT:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_CLIENT_CERT:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_SERVER_CERT:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_CLIENT_PRIV_KEY:
        /* Not handle currently */
        break;;
	case ESP_BLUFI_EVENT_RECV_SERVER_PRIV_KEY:
        /* Not handle currently */
        break;
    default:
        break;
    }
}
