#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"

#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "telnetd.h"

#define TAG "telnetd"

static StaticQueue_t txStaticQueue;
//EXT_RAM_ATTR uint8_t txQueueStorageArea[512];
EXT_RAM_BSS_ATTR uint8_t txQueueStorageArea[512];
static xQueueHandle tx_data_queue = NULL;

static int telnet_writefn(void* cookie, const char* data, int size)
{
    for(int i=0;i<size;i++)
        xQueueSend(tx_data_queue, &data[i], 0);
    return size;
}

#ifdef REDIRECT_STDIN

static StaticQueue_t rxStaticQueue;
//EXT_RAM_ATTR uint8_t rxQueueStorageArea[128];
EXT_RAM_BSS_ATTR uint8_t rxQueueStorageArea[128];
static xQueueHandle rx_data_queue = NULL;

static int telnet_readfn(void* cookie, char* data, int size)
{
    size_t rx_count = 0;
    uint8_t ub;

    while(xQueueReceive(rx_data_queue, &ub, 0)) {
        data[rx_count++] = ub;
        //if(ub == '\r' || ub == '\n')
        //    break;
        if(rx_count >= size)
            break;
    }

    return rx_count;
}

#endif

#define PORT 23
#define KEEPALIVE_IDLE              5
#define KEEPALIVE_INTERVAL          5
#define KEEPALIVE_COUNT             3

void telnetdTask(void *pvParameters)
{
    char addr_str[128];
    int addr_family = AF_INET;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;

    if(tx_data_queue == NULL)
        tx_data_queue = xQueueCreateStatic(512,
                                 sizeof(uint8_t),
                                 txQueueStorageArea,
                                 &txStaticQueue );
    else
        xQueueReset(tx_data_queue);

#ifdef REDIRECT_STDIN
    
    if(rx_data_queue == NULL)
        rx_data_queue = xQueueCreateStatic(128,
                                 sizeof(uint8_t),
                                 rxQueueStorageArea,
                                 &rxStaticQueue );
    else
        xQueueReset(rx_data_queue);

#endif
    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT); /* Port 23 */
        ip_protocol = IPPROTO_IP;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {

        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }

        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        //do_retransmit(sock);

        fflush(stdout);
        // standard IO streams are inherited when a task is created, so this needs to be done before creating other tasks:
        //stdout = fwopen(NULL, &telnet_writefn);
        fclose(stdout);
        stdout = funopen("telnet", NULL, telnet_writefn, NULL, NULL);
        // enable line buffering for this stream (to be similar to the regular UART-based output)
        static char stdout_buf[128];
        setvbuf(stdout, stdout_buf, _IOLBF, sizeof(stdout_buf));
#ifdef REDIRECT_STDIN
        fclose(stdin);
        stdin = funopen("telnet", &telnet_readfn, NULL, NULL, NULL);
        setvbuf(stdin, NULL, _IONBF, 0);
#endif
        for(;;) {
            fd_set fdsr;
            struct timeval tv;

            FD_ZERO(&fdsr);
            FD_SET(sock, &fdsr);

            tv.tv_sec = 0;
            tv.tv_usec = 10000; /* 10 ms */

            int r = select(sock+1, &fdsr, NULL, NULL, &tv);
            if(r > 0) {
                char rx_buffer[128];
                int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
                if(len < 0) {
                    ESP_LOGE(TAG, "recv failed: errno %d", errno);
                    break;
                } else if(len == 0) {
                    ESP_LOGI(TAG, "Connection closed");
                    break;
                } else {
                    //inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                    //ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);
                    //ESP_LOGI(TAG, "%s", rx_buffer);
#ifdef REDIRECT_STDIN
                    for(int i=0;i<len;i++)
                        xQueueSend(rx_data_queue, &rx_buffer[i], 0);
#endif
                }
            }

            uint8_t tx_buffer[512];
            size_t tx_count = 0;
            uint8_t ub;
            while(xQueueReceive(tx_data_queue, &ub, 0)) {
                tx_buffer[tx_count++] = ub;
                if(ub == '\r' || ub == '\n')
                    break;
                if(tx_count == 512)
                    break;
            }

            if(tx_count > 0) {
                r = send(sock, tx_buffer, tx_count, 0);
            }
        }

        shutdown(sock, 0);
        close(sock);

        fflush(stdout);
        fclose(stdout);
        _GLOBAL_REENT->_stdout = fopen("/dev/uart/0", "w");
        // enable line buffering for this stream (to be similar to the regular UART-based output)
        //static char stdout_buf[128];
        setvbuf(stdout, stdout_buf, _IOLBF, sizeof(stdout_buf));
#ifdef REDIRECT_STDIN
        fclose(stdin);
        _GLOBAL_REENT->_stdin = fopen("/dev/uart/0", "r");
        setvbuf(stdin, NULL, _IONBF, 0);
#endif
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}