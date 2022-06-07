/* Simple HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
//#include <esp_timer.h>
#include <sys/param.h>
#include "esp_netif.h"

#include <esp_http_server.h>

#include "http_server.h"

#include "network.h"
#include "esp32_malloc.h"

static const char *TAG = "HTTPServer";

#define CONFIG_PAGE "\
<html>\
<head></head>\
<meta name='viewport' content='width=device-width, initial-scale=1'>\
<body>\
<h1>ESP32 modbus_gateway Config</h1>\
<div id='config'>\
<script>\
if (window.location.search.substr(1) != '')\
{\
document.getElementById('config').display = 'none';\
document.body.innerHTML ='<h1>ESP32 modbus_gateway Config</h1>The new settings have been sent to the device...';\
setTimeout(\"location.href = '/'\",10000);\
}\
</script>\
<h2>AP Settings</h2>\
<form action='' method='GET'>\
<table>\
<tr>\
<td>SSID:</td>\
<td><input type='text' name='ap_ssid' value='%s'/></td>\
</tr>\
<tr>\
<td>Password:</td>\
<td><input type='text' name='ap_password' value='%s'/></td>\
</tr>\
<tr>\
<td></td>\
<td><input type='submit' value='Set' /></td>\
</tr>\
</table>\
<small>\
<i>Password: </i>less than 8 chars = open<br />\
</small>\
</form>\
\
<h2>STA Settings</h2>\
<form action='' method='GET'>\
<table>\
<tr>\
<td>SSID:</td>\
<td><input type='text' name='ssid' value='%s'/></td>\
</tr>\
<tr>\
<td>Password:</td>\
<td><input type='text' name='password' value='%s'/></td>\
</tr>\
<tr>\
<td></td>\
<td><input type='submit' value='Connect'/></td>\
</tr>\
\
</table>\
</form>\
\
<h2>STA Static IP Settings</h2>\
<form action='' method='GET'>\
<table>\
<tr>\
<td>Static IP:</td>\
<td><input type='text' name='staticip' value='%s'/></td>\
</tr>\
<tr>\
<td>Subnet Mask:</td>\
<td><input type='text' name='subnetmask' value='%s'/></td>\
</tr>\
<tr>\
<td>Gateway:</td>\
<td><input type='text' name='gateway' value='%s'/></td>\
</tr>\
<tr>\
<td></td>\
<td><input type='submit' value='Connect'/></td>\
</tr>\
\
</table>\
<small>\
<i>Leave it in blank if you want station get IP by DHCP.</i>\
</small>\
</form>\
\
<h2>Device Management</h2>\
<form action='' method='GET'>\
<table>\
<tr>\
<td>Reset Device:</td>\
<td><input type='submit' name='reset' value='Restart'/></td>\
</tr>\
</table>\
</form>\
<label>Chaekbox</label>\
<label><input type='checkbox' name='color1' value='blue' checked>Blue</label>\
<label><input type='checkbox' name='color2' value='yellow'>Yellow</label>\
<label><input type='checkbox' name='color3' value='red'>Red</label>\
<label><input type='checkbox' name='color4' value='green'>Green</label>\
</div>\
</body>\
</html>\
"

/* An HTTP GET handler */
static esp_err_t index_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
        buf = esp32_malloc(buf_len);
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Host: %s", buf);
        }
        esp32_free(buf);
    }

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = esp32_malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            char param1[64];
            char param2[64];
            //char param3[64];
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "ap_ssid", param1, sizeof(param1)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => ap_ssid=%s", param1);
                /*
                preprocess_string(param1);
                if (httpd_query_key_value(buf, "ap_password", param2, sizeof(param2)) == ESP_OK) {
                    ESP_LOGI(TAG, "Found URL query parameter => ap_password=%s", param2);
                    preprocess_string(param2);
                    int argc = 3;
                    char *argv[3];
                    argv[0] = "set_ap";
                    argv[1] = param1;
                    argv[2] = param2;
                    set_ap(argc, argv);
                    
                }
                */
                if (httpd_query_key_value(buf, "ap_password", param2, sizeof(param2)) == ESP_OK) {
                    ESP_LOGI(TAG, "Found URL query parameter => ap_password=%s", param2);
                }
            }
            if (httpd_query_key_value(buf, "ssid", param1, sizeof(param1)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => ssid=%s", param1);
                /*
                preprocess_string(param1);
                if (httpd_query_key_value(buf, "password", param2, sizeof(param2)) == ESP_OK) {
                    ESP_LOGI(TAG, "Found URL query parameter => password=%s", param2);
                    preprocess_string(param2);
                    int argc = 3;
                    char *argv[3];
                    argv[0] = "set_sta";
                    argv[1] = param1;
                    argv[2] = param2;
                    set_sta(argc, argv);
                    
                }
                */
                if (httpd_query_key_value(buf, "password", param2, sizeof(param2)) == ESP_OK) {
                    ESP_LOGI(TAG, "Found URL query parameter => password=%s", param2);

                    wifi_set_ssid(param1);
                    wifi_set_password(param2);
                    wifi_save_config();
                }
            }
            if (httpd_query_key_value(buf, "staticip", param1, sizeof(param1)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => staticip=%s", param1);
                /*
                preprocess_string(param1);
                if (httpd_query_key_value(buf, "subnetmask", param2, sizeof(param2)) == ESP_OK) {
                    ESP_LOGI(TAG, "Found URL query parameter => subnetmask=%s", param2);
                    preprocess_string(param2);
                    if (httpd_query_key_value(buf, "gateway", param3, sizeof(param3)) == ESP_OK) {
                        ESP_LOGI(TAG, "Found URL query parameter => gateway=%s", param3);
                        preprocess_string(param3);
                        int argc = 4;
                        char *argv[4];
                        argv[0] = "set_sta_static";
                        argv[1] = param1;
                        argv[2] = param2;
                        argv[3] = param3;
                        set_sta_static(argc, argv);
                        
                    }
                }
                */
                if (httpd_query_key_value(buf, "subnetmask", param2, sizeof(param2)) == ESP_OK) {
                    ESP_LOGI(TAG, "Found URL query parameter => subnetmask=%s", param2);
                }
            }
            if (httpd_query_key_value(buf, "reset", param1, sizeof(param1)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => reset=%s", param1);
            }
        }
        esp32_free(buf);
    }

    /* Send response with custom headers and body set as the
     * string passed in user context*/
    const char* resp_str = (const char*) req->user_ctx;
    httpd_resp_send(req, resp_str, strlen(resp_str));

    return ESP_OK;
}

static httpd_uri_t indexp = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_get_handler,
};

esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Page not found");
    return ESP_FAIL;
}

static httpd_handle_t s_server = NULL;

void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    const char *config_page_template = CONFIG_PAGE;
    char *config_page = esp32_malloc(strlen(config_page_template) + 128);

    char static_ip[16];
    char static_gateway[16];
    char static_netmask[16];

    esp_ip4_addr_t ip4_addr;
    ip4_addr = wifi_get_static_ip();
    esp_ip4addr_ntoa(&ip4_addr, static_ip, 16);

    ip4_addr = wifi_get_static_gateway();
    esp_ip4addr_ntoa(&ip4_addr, static_gateway, 16);

    ip4_addr = wifi_get_static_netmask();
    esp_ip4addr_ntoa(&ip4_addr, static_netmask, 16);

    sprintf(config_page, config_page_template, 
#ifdef CONFIG_EXAMPLE_ENABLE_WIFI_AP
            CONFIG_EXAMPLE_WIFI_AP_SSID, CONFIG_EXAMPLE_WIFI_AP_PASSWORD, 
#else
            "\0", "\0", 
#endif
            wifi_get_ssid(), wifi_get_password(),
            static_ip, static_netmask, static_gateway);
    indexp.user_ctx = config_page;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&s_server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(s_server, &indexp);
        return;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return;
}

void stop_webserver()
{
    // Stop the httpd server
    httpd_stop(s_server);
}
