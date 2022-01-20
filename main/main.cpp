/* FreeModbus Slave Example ESP32

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "esp_err.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_sntp.h"

#include "nvs_flash.h"

#include "mdns.h"
#include "esp_netif.h"

#include "driver/gpio.h"
#include "esp32/rom/gpio.h"
#include "driver/i2c.h"
#include "soc/rtc_io_reg.h"

#include "driver/adc.h"
#include "esp_adc_cal.h"

#include "network.h"
#include "telnetd.h"
#include "icmp_echo.h"

#include "esp_vfs.h"
#include "esp_vfs_dev.h"
#include "esp_console.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"

#include "led.h"
#include "i2cdev.h"
#include "ds1307.h"
#include "ds3231.h"
#include "ds18b20.h"
#include "pcf8574.hpp"
#include "lcd204-i2c.hpp"
//#include "sdmmc.h"

#include "modbus_tcp_slave.h"
#include "modbus_tcp2serial.h"
#include "modbus_data.h"

#include "esp_bt.h"
#include "esp_blufi_api.h"
#include "blufi_example.h"
#include "esp_blufi.h"

#include <lwip/dns.h>

#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>
#include <lwip/err.h>

#include "ota_ble.h"
#include "ota_https.h"

#include "http_server.h"

#define TAG "Main"

static nvs_handle my_nvs_handle;

#define HOSTNAME_MAX_LEN 32
#define DESCRIPTION_MAX_LEN 64

typedef struct {
    char hostname[HOSTNAME_MAX_LEN];
    char description[DESCRIPTION_MAX_LEN];
    bool enable_eth;
    bool enable_ntp;
    bool enable_mdns;
    bool enable_telnetd;
    bool enable_modbus_tcp2serial;
} sys_cfg_t;

static sys_cfg_t s_sys_cfg = {}; 

// https://blog.csdn.net/libin55/article/details/108206159

#define SYSLOG_QUEUE_SIZE   4096
#define SIZEOF_SYSLOG       sizeof(syslog_t)

/* The variable used to hold the queue's data structure. */
static StaticQueue_t xStaticQueue;
EXT_RAM_ATTR uint8_t ucQueueStorageArea[ SYSLOG_QUEUE_SIZE * SIZEOF_SYSLOG ];

static QueueHandle_t s_syslog_queue;

#define CMD_SYS_CFG  "sys_cfg"

void system_load_config()
{
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_nvs_handle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%d) opening NVS!\n", err);
        return;
    }

    size_t l = sizeof(sys_cfg_t);
    err = nvs_get_blob(my_nvs_handle, CMD_SYS_CFG, &s_sys_cfg, &l);
    if(err != ESP_OK) {
        ESP_LOGI(TAG, "No system config cached ...");
    }    

    nvs_close(my_nvs_handle);
}

void system_save_config()
{
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_nvs_handle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%d) opening NVS!\n", err);
        return;
    }

    err = nvs_set_blob(my_nvs_handle, CMD_SYS_CFG, &s_sys_cfg, sizeof(s_sys_cfg));
    if(err != ESP_OK)
        ESP_LOGE(TAG, "Fail save system config !!!");

    nvs_commit(my_nvs_handle);
    nvs_close(my_nvs_handle);
}

void system_factory_reset()
{
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_nvs_handle);

    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%d) opening NVS!\n", err);
        return;
    }

    nvs_erase_key(my_nvs_handle, CMD_SYS_CFG);

    nvs_commit(my_nvs_handle);
    nvs_close(my_nvs_handle);
}

/*
*
*/

#if CONFIG_MB_MDNS_IP_RESOLVER

#define MB_MDNS_PORT            (CONFIG_FMB_TCP_PORT_DEFAULT)

#if CONFIG_FMB_CONTROLLER_SLAVE_ID_SUPPORT
#define MB_DEVICE_ID (uint32_t)CONFIG_FMB_CONTROLLER_SLAVE_ID
#endif

// convert mac from binary format to string
static inline char* gen_mac_str(const uint8_t* mac, const char* pref, char* mac_str)
{
    sprintf(mac_str, "%s%02X%02X%02X%02X%02X%02X", pref, MAC2STR(mac));
    return mac_str;
}

static void start_mdns_service(void)
{
    char temp_str[32] = {0};
    uint8_t sta_mac[6] = {0};
    ESP_ERROR_CHECK(esp_read_mac(sta_mac, ESP_MAC_WIFI_STA));
    const char *hostname = s_sys_cfg.hostname;
    //initialize mDNS
    ESP_ERROR_CHECK(mdns_init());
    //set mDNS hostname (required if you want to advertise services)
    ESP_ERROR_CHECK(mdns_hostname_set(hostname));
    ESP_LOGI(TAG, "mdns hostname set to: [%s]", hostname);

    //set default mDNS instance name
    ESP_ERROR_CHECK(mdns_instance_name_set(hostname));

    //structure with TXT records
    mdns_txt_item_t serviceTxtData[] = {
        {"board","modbus_gateway"} // board=modbus_gateway
    };

    //initialize service
    ESP_ERROR_CHECK(mdns_service_add(hostname, "_modbus", "_tcp", MB_MDNS_PORT, serviceTxtData, 1));
    //add mac key string text item
    ESP_ERROR_CHECK(mdns_service_txt_item_set("_modbus", "_tcp", "mac", gen_mac_str(sta_mac, "\0", temp_str)));
}

static void stop_mdns_service(void)
{
    mdns_free();
}

#endif // CONFIG_MB_MDNS_IP_RESOLVER

static void initialize_gpio12()
{
    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = static_cast<gpio_int_type_t>(GPIO_PIN_INTR_DISABLE);
    //set as output mode
    io_conf.mode = GPIO_MODE_INPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = GPIO_SEL_12;
    //disable pull-down mode
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    //disable pull-up mode
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    gpio_set_direction(GPIO_NUM_12, GPIO_MODE_INPUT);
}

extern esp_blufi_callbacks_t example_callbacks;

static esp_err_t initialize_system(void)
{
    initialize_gpio12();

    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      result = nvs_flash_init();
    }
    ESP_RETURN_ON_FALSE((result == ESP_OK), ESP_ERR_INVALID_STATE,
                            TAG,
                            "nvs_flash_init fail, returns(0x%x).",
                            (uint32_t)result);
    result = esp_netif_init();
    ESP_RETURN_ON_FALSE((result == ESP_OK), ESP_ERR_INVALID_STATE,
                            TAG,
                            "esp_netif_init fail, returns(0x%x).",
                            (uint32_t)result);
    result = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE((result == ESP_OK), ESP_ERR_INVALID_STATE,
                            TAG,
                            "esp_event_loop_create_default fail, returns(0x%x).",
                            (uint32_t)result);

    snprintf(s_sys_cfg.hostname, HOSTNAME_MAX_LEN, "modbus_gateway");
    snprintf(s_sys_cfg.description, DESCRIPTION_MAX_LEN, "unknown");
    s_sys_cfg.enable_eth = false;
    s_sys_cfg.enable_ntp = true;
    s_sys_cfg.enable_mdns = true;
    s_sys_cfg.enable_telnetd = false;
    s_sys_cfg.enable_modbus_tcp2serial = true;

    system_load_config();
    wifi_load_config();
    eth_load_config();

    /* Create a queue capable of containing 10 uint64_t values. */
    s_syslog_queue = xQueueCreateStatic( SYSLOG_QUEUE_SIZE,
                                 SIZEOF_SYSLOG,
                                 ucQueueStorageArea,
                                 &xStaticQueue );

#if CONFIG_MB_MDNS_IP_RESOLVER
    // Start mdns service and register device
    if(s_sys_cfg.enable_mdns)
        start_mdns_service();
#endif
    if(s_sys_cfg.enable_eth)
        network_initialize(NETWORK_TYPE_ETH);
    else
        network_initialize(NETWORK_TYPE_WIFI);

    // This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
    // Read "Establishing Wi-Fi or Ethernet Connection" section in
    // examples/protocols/README.md for more information about this function.
    result = network_start();

ESP_LOGI(TAG, "Button level is %d", gpio_get_level(GPIO_NUM_12));

    if(gpio_get_level(GPIO_NUM_12) != 0) /* Button released ... */
        return ESP_OK;

/****************************************************************************
* This is a demo for bluetooth config wifi connection to ap. You can config ESP32 to connect a softap
* or config ESP32 as a softap to be connected by other device. APP can be downloaded from github
* android source code: https://github.com/EspressifApp/EspBlufi
* iOS source code: https://github.com/EspressifApp/EspBlufiForiOS
****************************************************************************/

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    result = esp_bt_controller_init(&bt_cfg);
    if(result != ESP_OK) {
        BLUFI_ERROR("%s initialize bt controller failed: %s\n", __func__, esp_err_to_name(result));
    }

    result = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if(result == ESP_OK) {
        result = esp_blufi_host_and_cb_init(&example_callbacks);
        if(result == ESP_OK) {
            BLUFI_INFO("BLUFI VERSION %04x\n", esp_blufi_get_version());
        } else {
            BLUFI_ERROR("%s initialise failed: %s\n", __func__, esp_err_to_name(result));
        }
    } else {    
        BLUFI_ERROR("%s enable bt controller failed: %s\n", __func__, esp_err_to_name(result));
    }

    return ESP_OK;
}

static esp_err_t deinitialize_system(void)
{
    esp_err_t err = ESP_OK;

    err = network_stop();
    ESP_RETURN_ON_FALSE((err == ESP_OK), ESP_ERR_INVALID_STATE,
                                   TAG,
                                   "network_disconnect fail, returns(0x%x).",
                                   (uint32_t)err);
    network_deinitialize();
    err = esp_event_loop_delete_default();
    ESP_RETURN_ON_FALSE((err == ESP_OK), ESP_ERR_INVALID_STATE,
                                       TAG,
                                       "esp_event_loop_delete_default fail, returns(0x%x).",
                                       (uint32_t)err);
    err = esp_netif_deinit();
    ESP_RETURN_ON_FALSE((err == ESP_OK || err == ESP_ERR_NOT_SUPPORTED), ESP_ERR_INVALID_STATE,
                                        TAG,
                                        "esp_netif_deinit fail, returns(0x%x).",
                                        (uint32_t)err);
    err = nvs_flash_deinit();
    ESP_RETURN_ON_FALSE((err == ESP_OK), ESP_ERR_INVALID_STATE,
                                TAG,
                                "nvs_flash_deinit fail, returns(0x%x).",
                                (uint32_t)err);
#if CONFIG_MB_MDNS_IP_RESOLVER
    stop_mdns_service();
#endif
    return err;
}

/*
*
*/

static int restart(int argc, char** argv)
{
    esp_restart();
}

static void register_restart()
{
    const esp_console_cmd_t cmd = {
        .command = "restart",
        .help = "Restart",
        .hint = NULL,
        .func = &restart,
        .argtable = NULL,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

static uint8_t atohex(char *s)
{
  uint8_t value = 0;
  if(!s)
    return 0;

  if(*s >= '0' && *s <= '9')
    value = (*s - '0') << 4;
  else if(*s >= 'A' && *s <= 'F')
    value = ((*s - 'A') + 10) << 4;
  else if(*s >= 'a' && *s <= 'f')
    value = ((*s - 'a') + 10) << 4;

  s++;

  if(*s >= '0' && *s <= '9')
    value |= (*s - '0');
  else if(*s >= 'A' && *s <= 'F')
    value |= ((*s - 'A') + 10);
  else if(*s >= 'a' && *s <= 'f')
    value |= ((*s - 'a') + 10);

  return value;
}

static int wifi(int argc, char** argv)
{
    esp_ip4_addr_t ip4_addr;
    char buf[16];

    if(argc <= 1) {
        printf("Wifi SSID : %s\n", wifi_get_ssid());
        printf("Wifi Password : %s\n", wifi_get_password());
        uint8_t *b = wifi_get_bssid();
        printf("Wifi BSSID :  %02x:%02x:%02x:%02x:%02x:%02x\n", b[0], b[1], b[2], b[3], b[4], b[5]);
        printf("Wifi Channel : %u\n", wifi_get_channel());
        uint8_t mac[6] = {0};
        //esp_read_mac(mac, ESP_MAC_WIFI_STA);
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        printf("Wifi STA MAC - %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        printf("Status : ");
        switch(wifi_get_status()) {
            case WIFI_STOPPED: printf("Stopped ...\n");
                break;
            case WIFI_STARTED: printf("Started ...\n");
                break;
            case WIFI_DISCONNECTED: printf("Disconnected ...\n");
                break;
            case WIFI_CONNECTING: printf("Connecting ...\n");
                break;
            case WIFI_CONNECTED: printf("Connected ...\n");
                ip4_addr = wifi_get_static_ip();
                if(ip4_addr.addr == 0) { /* 0.0.0.0 */
                    esp_netif_t *netif = get_network_netif_from_desc("sta");
                    esp_netif_ip_info_t ip_info;
                    esp_netif_get_ip_info(netif, &ip_info);
                    printf("DHCP IP : " IPSTR "\n", IP2STR(&ip_info.ip));
                    printf("Gateway : " IPSTR "\n", IP2STR(&ip_info.gw));
                } else {
                    printf("Static IP : %s\n", esp_ip4addr_ntoa(&ip4_addr, buf, 16));
                    ip4_addr = wifi_get_static_gateway();
                    printf("Gateway : %s\n", esp_ip4addr_ntoa(&ip4_addr, buf, 16));
                }
                break;
            case WIFI_CONNECT_FAIL: printf("Connect fail ...\n");
                break;
            case WIFI_INTERNAL_ERROR: printf("Internal error ...\n");
                 break;
        }
#ifdef CONFIG_EXAMPLE_ENABLE_WIFI_AP
        printf("Wifi AP SSID : %s\n", wifi_get_ap_ssid());
        printf("Wifi AP Password : %s\n", wifi_get_ap_password());
        printf("Wifi AP Channel : %u\n", wifi_get_ap_channel());
        //ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
        //ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP, mac));
        //printf("Wifi AP MAC - %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        uint8_t *m = wifi_get_ap_mac();
        printf("Wifi AP MAC :  %02x:%02x:%02x:%02x:%02x:%02x\n", m[0], m[1], m[2], m[3], m[4], m[5]);

        esp_netif_t *netif = get_network_netif_from_desc("ap");
        esp_netif_ip_info_t ip_info = {0};
        esp_netif_get_ip_info(netif, &ip_info);
        printf("IP : " IPSTR "\n", IP2STR(&ip_info.ip));
#endif
        return 0;
    }

    if(strcasecmp(argv[1], "ssid") == 0) {
        if(argc >= 3)
            wifi_set_ssid(argv[2]);
        else
            printf("Wifi SSID : %s\n", wifi_get_ssid());
    } else if(strcasecmp(argv[1], "password") == 0) {
        if(argc >= 3)
            wifi_set_password(argv[2]);
        else
            printf("Wifi Password : %s\n", wifi_get_password());
    } else if(strcasecmp(argv[1], "bssid") == 0) {
        if(argc >= 3) {
            uint8_t i = 0;
            uint8_t mac[6] = {0};
            char *s = strdup(argv[2]);
            char *t = strtok(s, ":");
            while(t != NULL && i < 6) {
                mac[i++] = atohex(t);
                t = strtok(NULL, ":");
            }
            wifi_set_bssid(mac);
        } else {
            uint8_t *mac = wifi_get_bssid();
            printf("Wifi BSSID :  %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
    } else if(strcasecmp(argv[1], "channel") == 0) {
        if(argc >= 3) {
            int v = atoi(argv[2]);
            if(v >= 0 && v <= 13)
                wifi_set_channel((uint8_t)(v & 0xff));
            else
                printf("Wifi Channel range from 0 to 13\n");
        } else
            printf("Wifi Channel : %u\n", wifi_get_ap_channel());
    } else if(strcasecmp(argv[1], "ap_mac") == 0) {
        if(argc >= 3) {
            uint8_t i = 0;
            uint8_t mac[6] = {0};
            char *s = strdup(argv[2]);
            char *t = strtok(s, ":");
            while(t != NULL && i < 6) {
                mac[i++] = atohex(t);
                t = strtok(NULL, ":");
            }
            wifi_set_ap_mac(mac);
        } else {
            uint8_t *mac = wifi_get_ap_mac();
            printf("Wifi AP MAC :  %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
    } else if(strcasecmp(argv[1], "ap_ssid") == 0) {
        if(argc >= 3)
            wifi_set_ap_ssid(argv[2]);
        else
            printf("Wifi AP SSID : %s\n", wifi_get_ap_ssid());
    } else if(strcasecmp(argv[1], "ap_password") == 0) {
        if(argc >= 3)
            wifi_set_ap_password(argv[2]);
        else
            printf("Wifi AP Password : %s\n", wifi_get_ap_password());
    } else if(strcasecmp(argv[1], "ap_channel") == 0) {
        if(argc >= 3) {
            int v = atoi(argv[2]);
            if(v >= 0 && v <= 13)
                wifi_set_ap_channel((uint8_t)(v & 0xff));
            else
                printf("Wifi AP Channel range from 0 to 13\n");
        } else
            printf("Wifi AP Channel : %u\n", wifi_get_ap_channel());
    } else if(strcasecmp(argv[1], "start") == 0) {
        network_start();
    } else if(strcasecmp(argv[1], "stop") == 0) {
        network_stop();
    } else if(strcasecmp(argv[1], "staticip") == 0) {
        if(argc >= 3) {            
            esp_err_t r = esp_netif_str_to_ip4(argv[2], &ip4_addr);
            if(r == ESP_OK)
                wifi_set_static_ip(&ip4_addr);
            else
                printf("Invalid IP : %s\n", argv[2]);
        } else {
            ip4_addr = wifi_get_static_ip();
            printf("Wifi static IP : %s\n", esp_ip4addr_ntoa(&ip4_addr, buf, 16));
        }
    } else if(strcasecmp(argv[1], "gateway") == 0) {
        if(argc >= 3) {            
            esp_err_t r = esp_netif_str_to_ip4(argv[2], &ip4_addr);
            if(r == ESP_OK)
                wifi_set_static_gateway(&ip4_addr);
            else
                printf("Invalid Gateway : %s\n", argv[2]);
        } else {            
            ip4_addr = wifi_get_static_gateway();
            printf("Wifi Gateway : %s\n", esp_ip4addr_ntoa(&ip4_addr, buf, 16));
        }
    } else if(strcasecmp(argv[1], "netmask") == 0) {
        if(argc >= 3) {            
            esp_err_t r = esp_netif_str_to_ip4(argv[2], &ip4_addr);
            if(r == ESP_OK)
                wifi_set_static_netmask(&ip4_addr);
            else
                printf("Invalid Netmask : %s\n", argv[2]);
        } else {            
            ip4_addr = wifi_get_static_netmask();
            printf("Wifi Netmask : %s\n", esp_ip4addr_ntoa(&ip4_addr, buf, 16));
        }
    } else if(strcasecmp(argv[1], "save") == 0) {
        wifi_save_config();
        printf("Wifi config saved ...\n");
    } else if(strcasecmp(argv[1], "reset") == 0) {
        wifi_factory_reset();
        printf("Reset done ...\n");
    } else
        printf("Unknown command !!!\n");

    return 0; 
}

static void register_wifi()
{
    const esp_console_cmd_t cmd = {
        .command = "wifi",
        .help = "wifi [ start | stop | ssid | password | bssid | channel | staticip | gateway | ap_mac | ap_ssid | ap_password | ap_channel | save | reset ] <string>",
        .hint = NULL,
        .func = &wifi,
        .argtable = NULL,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

static int eth(int argc, char** argv)
{
    esp_ip4_addr_t ip4_addr;
    char buf[16];

    if(argc <= 1) {
        uint8_t mac[6];
        //esp_wifi_get_mac(WIFI_IF_STA, mac);
        ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_ETH));
        printf("Ethernet MAC - %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);        
        printf("Status : ");
        switch(eth_get_status()) {
            case ETH_STOPPED: printf("Stopped ...\n");
                break;
            case ETH_STARTED: printf("Started ...\n");
                break;
            case ETH_DISCONNECTED: printf("Disconnected ...\n");
                break;
            case ETH_CONNECTING: printf("Connecting ...\n");
                break;
            case ETH_CONNECTED: printf("Connected ...\n");
                ip4_addr = eth_get_static_ip();
                if(ip4_addr.addr == 0) { /* 0.0.0.0 */
                    esp_netif_t *netif = get_network_netif_from_desc("eth");
                    esp_netif_ip_info_t ip_info;
                    esp_netif_get_ip_info(netif, &ip_info);
                    printf("DHCP IP : " IPSTR "\n", IP2STR(&ip_info.ip));
                    printf("Gateway : " IPSTR "\n", IP2STR(&ip_info.gw));
                } else {
                    printf("Static IP : %s\n", esp_ip4addr_ntoa(&ip4_addr, buf, 16));
                    ip4_addr = eth_get_static_gateway();
                    printf("Gateway : %s\n", esp_ip4addr_ntoa(&ip4_addr, buf, 16));
                }
                break;
            case ETH_CONNECT_FAIL: printf("Connect fail ...\n");
                break;
            case ETH_INTERNAL_ERROR: printf("Internal error ...\n");
                 break;
        }
        return 0;
    }

    if(strcasecmp(argv[1], "staticip") == 0) {
        if(argc >= 3) {            
            esp_err_t r = esp_netif_str_to_ip4(argv[2], &ip4_addr);
            if(r == ESP_OK)
                eth_set_static_ip(&ip4_addr);
            else
                printf("Invalid IP : %s\n", argv[2]);
        } else {
            ip4_addr = eth_get_static_ip();
            printf("Ethernet static IP : %s\n", esp_ip4addr_ntoa(&ip4_addr, buf, 16));
        }
    } else if(strcasecmp(argv[1], "gateway") == 0) {
        if(argc >= 3) {            
            esp_err_t r = esp_netif_str_to_ip4(argv[2], &ip4_addr);
            if(r == ESP_OK)
                eth_set_static_gateway(&ip4_addr);
            else
                printf("Invalid Gateway : %s\n", argv[2]);
        } else {            
            ip4_addr = eth_get_static_gateway();
            printf("Ethernet Gateway : %s\n", esp_ip4addr_ntoa(&ip4_addr, buf, 16));
        }
    } else if(strcasecmp(argv[1], "save") == 0) {
        eth_save_config();
        printf("Ethernet config saved ...\n");
    } else if(strcasecmp(argv[1], "reset") == 0) {
        eth_factory_reset();
        printf("Reset done ...\n");
    } else
        printf("Unknown command !!!\n");

    return 0; 
}

static void register_eth()
{
    const esp_console_cmd_t cmd = {
        .command = "eth",
        .help = "eth [ staticip | gateway | save | reset ] <string>",
        .hint = NULL,
        .func = &eth,
        .argtable = NULL,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

static int system(int argc, char** argv)
{
    if(argc <= 1) {
        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);
        printf("This is %s chip with %d CPU core(s), WiFi%s%s, ",
            CONFIG_IDF_TARGET,
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
        printf("silicon revision %d\n", chip_info.revision);
        printf("SDK Version : %s\n", esp_get_idf_version());
        printf("IRAM left %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        printf("SPIRAM left %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        printf("Hostname : %s\n", s_sys_cfg.hostname);
        printf("Description : %s\n", s_sys_cfg.description);
        printf("Ethernet : %s\n", s_sys_cfg.enable_eth ? "enable" : "disable");
        printf("NTP : %s\n", s_sys_cfg.enable_ntp ? "enable" : "disable");
        printf("mDNS : %s\n", s_sys_cfg.enable_mdns ? "enable" : "disable");
        printf("Telnetd : %s\n", s_sys_cfg.enable_telnetd ? "enable" : "disable");
        printf("Modbus TCP2Serial : %s\n", s_sys_cfg.enable_modbus_tcp2serial ? "enable" : "disable");
        printf("Voltage : %.2f V\n", input_reg_params.fp0);
        printf("Temperature : %.2f C\n", input_reg_params.fp1);
        return 0;
    }
 
    if(strcasecmp(argv[1], "hostname") == 0) {
        if(argc >= 3)
            snprintf(s_sys_cfg.hostname, HOSTNAME_MAX_LEN, "%s", argv[2]);
        else
            printf("Hostname : %s\n", s_sys_cfg.hostname);
    } else if(strcasecmp(argv[1], "description") == 0) {
        if(argc >= 3)
            snprintf(s_sys_cfg.description, DESCRIPTION_MAX_LEN, "%s", argv[2]);
        else
            printf("Description : %s\n", s_sys_cfg.description);
    } else if(strcasecmp(argv[1], "eth") == 0) {
        if(argc >= 3) {
            if(strcmp(argv[2], "enable") == 0)
                s_sys_cfg.enable_eth = true;
            else
                s_sys_cfg.enable_eth = false;
        } else
            printf("Ethernet : %s\n", s_sys_cfg.enable_eth ? "enable" : "disable");
    } else if(strcasecmp(argv[1], "ntp") == 0) {
        if(argc >= 3) {
            if(strcmp(argv[2], "enable") == 0)
                s_sys_cfg.enable_ntp = true;
            else
                s_sys_cfg.enable_ntp = false;
        } else
            printf("NTP : %s\n", s_sys_cfg.enable_ntp ? "enable" : "disable");
    } else if(strcasecmp(argv[1], "mdns") == 0) {
        if(argc >= 3) {
            if(strcmp(argv[2], "enable") == 0)
                s_sys_cfg.enable_mdns = true;
            else
                s_sys_cfg.enable_mdns = false;
        } else
            printf("mDNS : %s\n", s_sys_cfg.enable_mdns ? "enable" : "disable");
    } else if(strcasecmp(argv[1], "telnetd") == 0) {
        if(argc >= 3) {
            if(strcmp(argv[2], "enable") == 0)
                s_sys_cfg.enable_telnetd = true;
            else
                s_sys_cfg.enable_telnetd = false;
        } else
            printf("Telnetd : %s\n", s_sys_cfg.enable_telnetd ? "enable" : "disable");
    } else if(strcasecmp(argv[1], "modbus_tcp2serial") == 0) {
        if(argc >= 3) {
            if(strcmp(argv[2], "enable") == 0)
                s_sys_cfg.enable_modbus_tcp2serial = true;
            else
                s_sys_cfg.enable_modbus_tcp2serial = false;
        } else
            printf("MODBUS TCP2RTU : %s\n", s_sys_cfg.enable_modbus_tcp2serial ? "enable" : "disable");
    } else if(strcasecmp(argv[1], "save") == 0) {
        system_save_config();
        printf("System config saved ...\n");
    } else if(strcasecmp(argv[1], "reset") == 0) {
        system_factory_reset();
        printf("Reset done ...\n");
    } else
        printf("Unknown command !!!\n");

    return 0;
}

static void register_system()
{
    const esp_console_cmd_t cmd = {
        .command = "system",
        .help = "system [ hostname | description | eth | ntp | mdns | telnetd | modbus_tcp2serial | save | reset ] <string> | <enable | disable>",
        .hint = NULL,
        .func = &system,
        .argtable = NULL,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

static int mbtcp(int argc, char** argv)
{
    if(argc <= 1) {
        return 0;
    }
 
    if(strcasecmp(argv[1], "id") == 0) {
    }

    return 0;
}

static void register_mbtcp()
{
    const esp_console_cmd_t cmd = {
        .command = "mbtcp",
        .help = "mbtcp [ hostname ] <string>",
        .hint = NULL,
        .func = &mbtcp,
        .argtable = NULL,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

static void initialize_console()
{
    /* Drain stdout before reconfiguring it */
    fflush(stdout);
    fsync(fileno(stdout));

    /* Disable buffering on stdin and stdout */
    setvbuf(stdin, NULL, _IONBF, 0);
    //setvbuf(stdout, NULL, _IONBF, 0);

    /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
    esp_vfs_dev_uart_port_set_rx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CR);
    /* Move the caret to the beginning of the next line on '\n' */
    esp_vfs_dev_uart_port_set_tx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CRLF);

    /* Configure UART. Note that REF_TICK is used so that the baud rate remains
     * correct while APB frequency is changing in light sleep mode.
     */
    const uart_config_t uart_config = {
            .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 122,
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
        .source_clk = UART_SCLK_REF_TICK,
#else
        .source_clk = UART_SCLK_XTAL,
#endif
    };

    /* Install UART driver for interrupt-driven reads and writes */
    ESP_ERROR_CHECK( uart_driver_install(UART_NUM_0,
            256, 0, 0, NULL, 0) );
    ESP_ERROR_CHECK( uart_param_config(UART_NUM_0, &uart_config) );

    /* Tell VFS to use UART driver */
    esp_vfs_dev_uart_use_driver(UART_NUM_0);

    /* Initialize the console */
    esp_console_config_t console_config;
    console_config.max_cmdline_args = 8;
    console_config.max_cmdline_length = 256;
#if CONFIG_LOG_COLORS
    console_config.hint_color = atoi(LOG_COLOR_CYAN);
#endif

    ESP_ERROR_CHECK( esp_console_init(&console_config) );

    /* Configure linenoise line completion library */
    /* Enable multiline editing. If not set, long commands will scroll within
     * single line.
     */
    linenoiseSetMultiLine(1);

    /* Tell linenoise where to get command completions and hints */
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback*) &esp_console_get_hint);

    /* Set command history size */
    linenoiseHistorySetMaxLen(100);

    /* Set command maximum length */
    //linenoiseSetMaxLineLen(console_config.max_cmdline_length);

    /* Don't return empty lines */
    linenoiseAllowEmpty(false);
}

void consoleTask(void *pvParameters){
    initialize_console();

    esp_console_register_help_command();

    register_restart();
    register_wifi();
    register_eth();
    register_ping();
    register_mbtcp();
    register_system();

    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    const char* prompt = LOG_COLOR_I "jungle> " LOG_RESET_COLOR;
    /*
    printf("\n"
           "This is an example of ESP-IDF console component.\n"
           "Type 'help' to get the list of commands.\n"
           "Use UP/DOWN arrows to navigate through command history.\n"
           "Press TAB when typing command name to auto-complete.\n");
    */
    /* Figure out if the terminal supports escape sequences */
    int probe_status = linenoiseProbe();
    if (probe_status) { /* zero indicates success */
    /*
        printf("\n"
               "Your terminal application does not support escape sequences.\n"
               "Line editing and history features are disabled.\n"
               "On Windows, try using Putty instead.\n");
    */
        linenoiseSetDumbMode(1);
#if CONFIG_LOG_COLORS
        /* Since the terminal doesn't support escape sequences,
         * don't use color codes in the prompt.
         */
        prompt = "jungle> ";
#endif //CONFIG_LOG_COLORS
    }


    while (1) {
        char* line = linenoise(prompt);
        if (line == NULL) { /* Ignore empty lines */
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        /* Add the command to the history */
        linenoiseHistoryAdd(line);

        /* Try to run the command */
        int ret;
        esp_err_t err = esp_console_run(line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("Not found\n");
        } else if (err == ESP_ERR_INVALID_ARG) {
            // command was empty
        } else if (err == ESP_OK && ret != ESP_OK) {
            printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(err));
        } else if (err != ESP_OK) {
            printf("Internal error: %s\n", esp_err_to_name(err));
        }
        /* linenoise allocates line buffer on the heap, so need to free it */
        linenoiseFree(line);
    }

     uart_driver_delete(UART_NUM_0);
}

static void check_efuse()
{
    //Check TP is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
        printf("eFuse Two Point: Supported\n");
    } else {
        printf("eFuse Two Point: NOT supported\n");
    }

    //Check Vref is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK) {
        printf("eFuse Vref: Supported\n");
    } else {
        printf("eFuse Vref: NOT supported\n");
    }
}

static void print_char_val_type(esp_adc_cal_value_t val_type)
{
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        printf("Characterized using Two Point Value\n");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        printf("Characterized using eFuse Vref\n");
    } else {
        printf("Characterized using Default Vref\n");
    }
}

void adcTask(void *pvParams)
{
    #define DEFAULT_VREF    1100
    #define NO_OF_SAMPLES   64 //Multisampling

    static const adc_unit_t unit = ADC_UNIT_1;
    static const adc_bits_width_t width = ADC_WIDTH_BIT_12;

    static esp_adc_cal_characteristics_t *adc_chars;
    static adc_channel_t channel = ADC_CHANNEL_0;     //GPIO36 if ADC1
    static adc_atten_t atten = ADC_ATTEN_DB_6;

    //Check if Two Point or Vref are burned into eFuse
    check_efuse();

    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten((adc1_channel_t)channel, atten);

    //Characterize ADC
    adc_chars = (esp_adc_cal_characteristics_t*)calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);
    print_char_val_type(val_type);

    while(1) {
        uint32_t adc_reading = 0;
        //Multisampling
        for (int i = 0; i < NO_OF_SAMPLES; i++) {
            adc_reading += adc1_get_raw((adc1_channel_t)channel);
        }
        adc_reading /= NO_OF_SAMPLES;
        //Convert adc_reading to voltage in mV
        uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);
        //printf("Raw: %d\tVoltage: %dmV\n", adc_reading, voltage);
        input_reg_params.fp0 = (voltage * 17.35f) / 1000.0f;
        //printf("input_reg_params.fp0 = %f\n", input_reg_params.fp0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

void tempTask(void *pvParameters) {
    ds18b20_init(GPIO_NUM_5);

    while(1) {
        float r = ds18b20_get_temp();
        //printf("Temperature: %0.1f\n", r);
        if(r >= -55.0 && r <= 125.0) {
            //portENTER_CRITICAL(&tcp_slave_param_lock);
            input_reg_params.fp1 = r;
            //portEXIT_CRITICAL(&tcp_slave_param_lock);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static SemaphoreHandle_t semReadExtGpio = NULL; // Read pcf8574[0]
static SemaphoreHandle_t semWriteExtGpio = NULL; // Write pcf8574[1]

static void IRAM_ATTR gpio34_isr_handler(void* arg)
{
    static BaseType_t xHigherPriorityTaskWoken;

    /* Unblock the task by releasing the semaphore. */
    xSemaphoreGiveFromISR(semReadExtGpio, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void IRAM_ATTR gpio35_isr_handler(void* arg)
{
}

static void initialize_gpio_isr()
{
#define GPIO_INPUT_IO_34     34
#define GPIO_INPUT_IO_35     35

#define ESP_INTR_FLAG_DEFAULT 0
    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);

    gpio_config_t io_conf;
    //interrupt of rising or falling edge
    io_conf.intr_type = (gpio_int_type_t)(GPIO_PIN_INTR_NEGEDGE); /* Falling edge */
    //bit mask of the pins, use GPIO4/5 here
    io_conf.pin_bit_mask = (1ULL<<GPIO_INPUT_IO_34);
    //set as input mode    
    io_conf.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf.pull_up_en = (gpio_pullup_t)(1);
    gpio_config(&io_conf);

    //hook isr handler for specific gpio pin
    gpio_isr_handler_add((gpio_num_t)(GPIO_INPUT_IO_34), gpio34_isr_handler, (void *)(GPIO_INPUT_IO_34));    


    //interrupt of rising or falling edge
    io_conf.intr_type = (gpio_int_type_t)(GPIO_PIN_INTR_NEGEDGE); /* Falling edge */
    //bit mask of the pins, use GPIO4/5 here
    io_conf.pin_bit_mask = (1ULL<<GPIO_INPUT_IO_35);
    //set as input mode    
    io_conf.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf.pull_up_en = (gpio_pullup_t)(1);
    gpio_config(&io_conf);

    //hook isr handler for specific gpio pin
    gpio_isr_handler_add((gpio_num_t)(GPIO_INPUT_IO_35), gpio35_isr_handler, (void *)(GPIO_INPUT_IO_35));    
}

#define I2C_MASTER_SCL_IO           (gpio_num_t)32          /*!< gpio number for I2C master clock IO21*/
#define I2C_MASTER_SDA_IO           (gpio_num_t)21          /*!< gpio number for I2C master data  IO15*/
#define I2C_MASTER_NUM              I2C_NUM_0               /*!< I2C port number for master dev */
#if 0
#define I2C_MASTER_TX_BUF_DISABLE   0                       /*!< I2C master do not need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0                       /*!< I2C master do not need buffer */
#define I2C_MASTER_FREQ_HZ          200000                  /*!< I2C master clock frequency */
#endif
static void initialize_i2c0() {    
    /* gpio32 route to digital io_mux */
    REG_CLR_BIT(RTC_IO_XTAL_32K_PAD_REG, RTC_IO_X32P_MUX_SEL);
#if 0
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)I2C_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = (gpio_num_t)I2C_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
#else
    i2c_master_init(I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
#endif
}

/* Works on both DS1307 & DS3231 */
static i2c_dev_t ds1307_dev = {};

void rtcTimeTask(void *pvParameters) {
    static bool isRtcTime = false;    
    if(ds1307_init_desc(&ds1307_dev, I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO) != ESP_OK)
        ESP_LOGE(TAG, "RTC clock initialize failed !!!");

    uint8_t retry = 0;

    TickType_t xLastWakeTime = xTaskGetTickCount();
    while(1) {
        struct tm time;
        if (ds1307_get_time(&ds1307_dev, &time) != ESP_OK) {
            ESP_LOGE(TAG, "RTC read failed !!!");
            retry++;
        } else {
            retry = 0;
            ESP_LOGI(TAG, "RTC %04d-%02d-%02d %02d:%02d:%02d", 
                time.tm_year, time.tm_mon + 1,
                time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);

                time.tm_year -= 1900;
                time_t t = mktime(&time);
                //printf("Setting time: %s", asctime(&time));
                struct timeval now = { .tv_sec = t, .tv_usec = 0 };

                setenv("TZ", "GMT-8", 1);
                tzset();
                settimeofday(&now, NULL);

            if(!isRtcTime) {
                isRtcTime = true;
                break;
            }
        }

        if(retry >= 5) /* Something wrong !!! abort ... */
            break;
        vTaskDelayUntil(&xLastWakeTime, 1000 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");

    struct tm time;
    gmtime_r((const time_t *)&(tv->tv_sec), &time);
    ESP_LOGI(TAG, "NTP %04d-%02d-%02d %02d:%02d:%02d", 
                time.tm_year + 1900, time.tm_mon + 1,
                time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);

    if(ds1307_set_time(&ds1307_dev, &time) != ESP_OK) {
        ESP_LOGE(TAG, "RTC set time failed !!!");
    }
}

static void dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    ip_addr_t addr;
    ESP_LOGI(TAG, "%sfound host ip %s", ipaddr == NULL?"NOT ":"", name);
    
    if(ipaddr == NULL)
        return;

    addr = *ipaddr;
    ESP_LOGI(TAG, "DNS found IP: %i.%i.%i.%i, host name: %s",
            ip4_addr1(&addr.u_addr.ip4),
            ip4_addr2(&addr.u_addr.ip4),
            ip4_addr3(&addr.u_addr.ip4),
            ip4_addr4(&addr.u_addr.ip4),
            name);
}

static void dns_query()
{
    struct addrinfo hints = {};

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    struct addrinfo *res;
    struct in_addr *addr;
    int err = getaddrinfo("pool.ntp.org", "80", &hints, &res);
    if(err != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS lookup failed !!! err=%d, res=%p", err, res);
    } else {
        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        ESP_LOGI(TAG, "DNS lookup. IP=%s", inet_ntoa(*addr));
    }    
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
}

void sntpTask(void *pvParameters) {
    time_t now;
    struct tm timeinfo;

    while(1) {
        if(wifi_get_status() == WIFI_CONNECTED ||
            eth_get_status() == ETH_CONNECTED) {
            break;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    ip_addr_t dnsserver;
    
    inet_pton(AF_INET, "8.8.8.8", &dnsserver);
    dns_setserver(0, &dnsserver);
    inet_pton(AF_INET, "8.8.4.4", &dnsserver);
    dns_setserver(1, &dnsserver);

    ip_addr_t addr;
    err_t err = dns_gethostbyname("pool.ntp.org", &addr, &dns_found_cb, NULL);
    if(err != ESP_OK && err != ERR_INPROGRESS)
        ESP_LOGE(TAG, "DNS lookup failed !!!");
    
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    time(&now);
    localtime_r(&now, &timeinfo);

    // Is time set? If not, tm_year will be (1970 - 1900).
    if(timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGI(TAG, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
        // wait for time to be set
        int retry = 0;
        const int retry_count = 30;
        while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
            ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
        time(&now);
        localtime_r(&now, &timeinfo);
    } else {
        /* System time from RTC ... Nothing to do here */
    }

    char strftime_buf[64];
#if 0
    // Set timezone to Eastern Standard Time and print local time
    setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0", 1);
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in New York is: %s", strftime_buf);
#endif
    // Set timezone to China Standard Time
    setenv("TZ", "GMT-8", 1);
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in Taipei is: %s", strftime_buf);

    vTaskDelete(NULL);
}

typedef enum { SYSLOG_IN_ON = 1, SYSLOG_IN_OFF, SYSLOG_OUT_ON, SYSLOG_OUT_OFF, SYSLOG_ALARM, SYSLOG_SYSTEM } syslog_e;

static const char *syslog2str(syslog_e event)
{
    switch(event) {
        case SYSLOG_IN_ON:
            return "Input ON";
        case SYSLOG_IN_OFF:
            return "Input OFF";
        case SYSLOG_OUT_ON:
            return "Output ON";
        case SYSLOG_OUT_OFF:
            return "Output OFF";
        case SYSLOG_ALARM:
            return "Alarm";
        case SYSLOG_SYSTEM:
            return "System";
        default:
            return "Unknown";
    }
}

int64_t xx_time_get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL));
}

void initialize_ext_gpio() {
    semReadExtGpio = xSemaphoreCreateBinary(); // Read pcf8574[0]
    semWriteExtGpio = xSemaphoreCreateBinary(); // Read pcf8574[1]
}

void extGpioTask(void *pvParameters) {
    static PCF8574 *pcf8574 = nullptr;
    pcf8574 = new PCF8574[2];
#ifdef CONFIG_MB_SLAVE_EXT_INPUT
    pcf8574[0].begin(0x40);
    pcf8574[0].allPinsMode(INPUT_PULLUP);
    discrete_reg_params.byte0 = pcf8574[0].read();
#endif
    pcf8574[1].begin(0x42);
    pcf8574[1].allPinsMode(OUTPUT_PULLUP);
    pcf8574[1].write(~(coil_reg_params.byte0)); /**/

    while(1) {
#ifdef CONFIG_MB_SLAVE_EXT_INPUT
#if 0
        //if(xSemaphoreTake(semReadExtGpio, 10 / portTICK_PERIOD_MS) == pdTRUE) {
#else
        xSemaphoreTake(semReadExtGpio, 10 / portTICK_PERIOD_MS);
        if(1) {
#endif
            uint8_t b = discrete_reg_params.byte0;
            discrete_reg_params.byte0 = ~(pcf8574[0].read());

            uint8_t x = b ^ discrete_reg_params.byte0;
            if(x != 0) {
                uint32_t timestamp = (uint32_t)time(NULL);
                //int64_t timestamp = xx_time_get_time();
                for(uint8_t i=0;i<8;i++) {
                    if(x & 0x01) {
                        syslog_e event;
                        if(discrete_reg_params.byte0 & (1 << i))
                            event = SYSLOG_IN_ON;
                        else
                            event = SYSLOG_IN_OFF;
                        // Log timestamp, evt, i
                        printf("[ %10d ] : %d - input[%d] = %d\n", timestamp, event, i, (discrete_reg_params.byte0 >> i) & 0x01);

                        syslog_t log;
                        log.timestamp = timestamp;
                        log.event = event;
                        log.index = (discrete_reg_params.byte0 >> i) & 0x01;

                        xQueueSend(s_syslog_queue, &log, 10 / portTICK_PERIOD_MS);

                        ESP_LOGI(TAG, "Syslog queue space available is %d", uxQueueSpacesAvailable(s_syslog_queue));
                    }
                    x = (x >> 1);
                }

                printf("discrete_reg_params.byte0 = 0x%02x\n", discrete_reg_params.byte0);
            }
        }
#endif
        if(xSemaphoreTake(semWriteExtGpio, 10 / portTICK_PERIOD_MS) == pdTRUE) {
            static uint8_t byte0 = 0;
            uint8_t x = byte0 ^ coil_reg_params.byte0;
            if(x != 0) {
                uint32_t timestamp = (uint32_t)time(NULL);
                //int64_t timestamp = xx_time_get_time();
                for(uint8_t i=0;i<8;i++) {
                    if(x & 0x01) {
                        syslog_e event;
                        if(coil_reg_params.byte0 & (1 << i))
                            event = SYSLOG_OUT_ON;
                        else
                            event = SYSLOG_OUT_OFF;
                        // Log timestamp, evt, i
                        printf("[ %10d ] : %d - output[%d] = %d\n", timestamp, event, i, (coil_reg_params.byte0 >> i) & 0x01);

                        syslog_t log;
                        log.timestamp = timestamp;
                        log.event = event;
                        log.index = (coil_reg_params.byte0 >> i) & 0x01;

                        xQueueSend(s_syslog_queue, &log, 10 / portTICK_PERIOD_MS);

                        ESP_LOGI(TAG, "Syslog queue space available is %d", uxQueueSpacesAvailable(s_syslog_queue));
                    }
                    x = (x >> 1);
                }

                printf("coil_reg_params.byte0 = 0x%02x\n", coil_reg_params.byte0);
                byte0 = coil_reg_params.byte0;
            }

            pcf8574[1].write(~(coil_reg_params.byte0));
        }
    }

    delete [] pcf8574;
}

void ledTask(void *pvParameters) {
    led_init(&s_buzzer, (gpio_num_t)GPIO_NUM_23);
    led_init(&s_led25, (gpio_num_t)GPIO_NUM_25);
    led_init(&s_led27, (gpio_num_t)GPIO_NUM_27);

    TickType_t xLastWakeTime = xTaskGetTickCount();
    while(1) {
        led_handler(&s_buzzer);
        led_handler(&s_led25);
        led_handler(&s_led27);
        vTaskDelayUntil(&xLastWakeTime, 10 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

void appTask(void *pvParameters) {
    //ota_ble_run();
    //ota_https_run("192.168.31.72", 8070, "modbus_gateway.bin");

    LCD204_I2C_Init(0x4e);
    LCD204_I2C_Display();

    LCD204_I2C_PrintRow(0, "modbus_gateway");
    LCD204_I2C_PrintRow(1, "Version : v0.3");
    LCD204_I2C_PrintRow(2, "Author : Steve Chang");
    LCD204_I2C_PrintRow(3, "17th Nov. 2021");

    while(1) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// An example application of Modbus slave. It is based on freemodbus stack.
// See deviceparams.h file for more information about assigned Modbus parameters.
// These parameters can be accessed from main application and also can be changed
// by external Modbus master host.
extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(initialize_system());

    // Set UART log level
    esp_log_level_set(TAG, ESP_LOG_INFO);

    xTaskCreatePinnedToCore(ledTask, "ledTask", 2048, NULL, 3, NULL, 0);

    if(s_sys_cfg.enable_eth == false) {
        led_sos_beep(&s_buzzer);
        led_slow_flash(&s_led27);
    }
    
    //initialize_sdmmc();
    initialize_gpio_isr();
    xTaskCreatePinnedToCore(&adcTask, "adcTask", 3072, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(&tempTask, "tempTask", 3072, NULL, 4, NULL, 0);

    initialize_i2c0(); /* Must before ds1307 RTC & pcf8574 gpio */
    xTaskCreatePinnedToCore(&rtcTimeTask, "rtcTimeTask", 3072, NULL, 4, NULL, 1);

    initialize_sntp();
    xTaskCreatePinnedToCore(&sntpTask, "sntpTask", 3072, NULL, 4, NULL, 0);

    initialize_ext_gpio();
    xTaskCreatePinnedToCore(&extGpioTask, "extGpioTask", 4096, NULL, 4, NULL, 1);

    xTaskCreatePinnedToCore(&mbTcpSlaveTask, "mbTcpSlaveTask", 8192, semWriteExtGpio, 5, NULL, 0);
    //xTaskCreatePinnedToCore(&mbRtuMasterTask, "mbRtuMasterTask", 8192, NULL, 5, NULL, 0);

    /* RS485 9600 8E1 */
    initialize_modbus_tcp2serial();
    xTaskCreatePinnedToCore(&mbTcp2Serial_task, "mbTcp2Serial_task", 3072, NULL, 4, NULL, 0);

    xTaskCreatePinnedToCore(&consoleTask, "consoleTask", 3072, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(&telnetdTask, "telnetdTask", 4096, NULL, 2, NULL, 1);

    //xTaskCreatePinnedToCore(&appTask, "appTask", 4096, NULL, 3, NULL, 0);

#ifdef CONFIG_EXAMPLE_ENABLE_WIFI_AP
    start_webserver();
#endif

    while(1) {
#if 1        
        if(s_sys_cfg.enable_eth == false) {
            wifi_ap_record_t ap_info;
            esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
            if(err == ESP_OK) {
                //ESP_LOGI(TAG, "rssi : %d", ap_info.rssi);
                if(ap_info.rssi < -60)
                    led_slow_flash(&s_buzzer);
                else if(ap_info.rssi < -70)
                    led_normal_flash(&s_buzzer);
                else if(ap_info.rssi < -80)
                    led_fast_flash(&s_buzzer);
                else
                    led_off(&s_buzzer);
            } else
                led_sos_beep(&s_buzzer);
        }
#endif
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    ESP_ERROR_CHECK(deinitialize_system());
}

/*

avahi-browse -t _modbus._tcp -r

+   wlo1 IPv4 modbus_gateway                                     _modbus._tcp         local
=   wlo1 IPv4 modbus_gateway                                     _modbus._tcp         local
   hostname = [modbus_gateway.local]
   address = [192.168.31.147]
   port = [502]
   txt = ["board=modbus_gateway" "mac=AC67B22A886C"]
+   wlo1 IPv4 airCon01                                      _modbus._tcp         local
=   wlo1 IPv4 airCon01                                      _modbus._tcp         local
   hostname = [airCon01.local]
   address = [192.168.31.203]
   port = [502]
   txt = ["board=modbus_gateway" "mac=AC67B22A8440"]

*/