#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "sdkconfig.h"

#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"

#if CONFIG_EXAMPLE_CONNECT_WIFI
#include "esp_wifi.h"
#endif
#include "network.h"

#define TAG "ota_https"

extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}

#define HASH_LEN 32

static void print_sha256(const uint8_t *image_hash, const char *label)
{
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s %s", label, hash_print);
}

static void get_sha256_of_partitions(void)
{
    uint8_t sha_256[HASH_LEN] = { 0 };
    esp_partition_t partition;

    // get sha256 digest for bootloader
    partition.address   = ESP_BOOTLOADER_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_OFFSET;
    partition.type      = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    // get sha256 digest for running partition
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");
}

#define OTA_URL_SIZE 256

const char *get_firmware_url(const char *host, uint16_t port, const char *firmware)
{
	static char url[OTA_URL_SIZE] = {};

	snprintf(url, OTA_URL_SIZE, "https://%s:%u/%s", host, port, firmware);

	return url;
}

void ota_https_task(void *pvParameter)
{
    ESP_LOGE(TAG, "Starting OTA HTTPS");

    const char *url = (const char *)pvParameter;

    esp_netif_t *netif = get_network_netif();
    if (netif == NULL) {
        ESP_LOGE(TAG, "Can't find netif from interface description");
        goto _abort;
    }
    struct ifreq ifr;
    esp_netif_get_netif_impl_name(netif, ifr.ifr_name);
    ESP_LOGI(TAG, "Bind interface name is %s", ifr.ifr_name);

    esp_http_client_config_t config = {
        .url = 0,
        .cert_pem = (char *)server_cert_pem_start,
        .event_handler = _http_event_handler,
        .keep_alive_enable = true,
        .if_name = &ifr,
    };

    config.url = url;
#if 1
    config.skip_cert_common_name_check = true;
#endif
    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };
    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Firmware upgrade failed");
    }
_abort:
    vTaskDelete(NULL);
}

void ota_https_run(const char *host, uint16_t port, const char *firmware)
{
	get_sha256_of_partitions();

    while(1) {
        if(wifi_get_status() == WIFI_CONNECTED ||
            eth_get_status() == ETH_CONNECTED) {
            break;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    /* Ensure to disable any WiFi power save mode, this allows best throughput
     * and hence timings for overall OTA operation.
     */
    esp_wifi_set_ps(WIFI_PS_NONE);
#endif

    const char *url = get_firmware_url(host, port, firmware);

    xTaskCreatePinnedToCore(&ota_https_task, "ota_https_task", 8192, (void *)url, 5, NULL, 1);
}