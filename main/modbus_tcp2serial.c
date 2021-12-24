#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "lwip/sockets.h"

#include "sdkconfig.h"

#include "esp32_malloc.h"
#include "esp_modbus_master.h"

#define MB_PORT_NUM     (CONFIG_MB_UART_PORT_NUM)   // Number of UART port used for Modbus connection
#define MB_DEV_SPEED    (CONFIG_MB_UART_BAUD_RATE)  // The communication speed of the UART

#define MB_TCP_PORT_NUMBER      (CONFIG_FMB_TCP_PORT_DEFAULT)

static const char *TAG = "tcp2serial";

#define SENSE_MB_CHECK(a, ret_val, str, ...) \
    if (!(a)) { \
        ESP_LOGE(TAG, "%s(%u): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
        return (ret_val); \
    }

//
// MODBUS MBAP offsets
//
#define MB_TCP_TID 0
#define MB_TCP_PID 2
#define MB_TCP_LEN 4
#define MB_TCP_UID 6
#define MB_TCP_FUNC 7
#define MB_TCP_REGISTER_START 8
#define MB_TCP_REGISTER_NUMBER 10

//static uint8_t s_tcp_tx_buf[128];
//static uint8_t s_tcp_rx_buf[128];

// Function code
#define MB_FUNC_READ_COILS 1
#define MB_FUNC_READ_DISCRETE_INPUTS 2
#define MB_FUNC_READ_HOLDING_REGISTERS 3
#define MB_FUNC_READ_INPUT_REGISTER 4

#define MB_FUNC_WRITE_SINGLE_COIL 5
#define MB_FUNC_WRITE_SINGLE_REGISTER 6
#define MB_FUNC_READ_EXCEPTION_STATUS 7
#define MB_FUNC_DIAGNOSTIC 8

esp_err_t modbus_serial_master_init(uart_port_t port, int baudrate, uart_parity_t parity)
{
    mb_communication_info_t comm = {
            .port = port,
#if CONFIG_MB_COMM_MODE_RTU            
            .mode = MB_MODE_RTU,
#elif CONFIG_MB_COMM_MODE_ASCII
            .mode = MB_MODE_ASCII,
#endif
            .baudrate = baudrate,
            .parity = parity
    };
    void* master_handler = NULL;

    esp_err_t err = mbc_master_init(MB_PORT_SERIAL_MASTER, &master_handler);
    SENSE_MB_CHECK((master_handler != NULL), ESP_ERR_INVALID_STATE,
                                "mb controller initialization fail.");
    SENSE_MB_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE,
                            "mb controller initialization fail, returns(0x%x).",
                            (uint32_t)err);
    err = mbc_master_setup((void*)&comm);
    SENSE_MB_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE,
                            "mb controller setup fail, returns(0x%x).",
                            (uint32_t)err);
    err = mbc_master_start();
    SENSE_MB_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE,
                            "mb controller start fail, returns(0x%x).",
                            (uint32_t)err);
    // Set UART pin numbers
    err = uart_set_pin(port, CONFIG_MB_UART_TXD, CONFIG_MB_UART_RXD,
                                    CONFIG_MB_UART_RTS, UART_PIN_NO_CHANGE);
    SENSE_MB_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE,
            "mb serial set pin failure, uart_set_pin() returned (0x%x).", (uint32_t)err); 
    // Set driver mode to Half Duplex
    err = uart_set_mode(port, UART_MODE_RS485_HALF_DUPLEX);
    SENSE_MB_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE,
            "mb serial set mode failure, uart_set_mode() returned (0x%x).", (uint32_t)err);
    vTaskDelay(5);

    return err;
}

static uint16_t s_tcp_port = 503;

static void modbus_tcp_slave_init(uint16_t port)
{
	s_tcp_port = port;
}

static xSemaphoreHandle mbc_mutex;

void initialize_modbus_tcp2serial()
{
    mbc_mutex = xSemaphoreCreateMutex();

    modbus_serial_master_init(MB_PORT_NUM, MB_DEV_SPEED, UART_PARITY_EVEN);
	modbus_tcp_slave_init(MB_TCP_PORT_NUMBER + 1);
}

#define MAX_TCP_CONNECTIONS 8
static size_t s_num_tcp_connections = 0;

typedef struct {
    int sock;
    char addr_str[32];
} tcp_task_t;

static void _tcp_task(void *pvParameters)
{
    tcp_task_t *p = (tcp_task_t *)pvParameters;
    int sock = p->sock;
    char addr_str[32];
    snprintf(addr_str, sizeof(addr_str), "%s", p->addr_str);

#define TCP_TX_BUF_SIZE 256
#define TCP_RX_BUF_SIZE 256

    uint8_t tcp_tx_buf[TCP_TX_BUF_SIZE];
    uint8_t tcp_rx_buf[TCP_RX_BUF_SIZE];

    while (1) {
        int len = recv(sock, tcp_rx_buf, TCP_RX_BUF_SIZE - 1, 0);            
        if(len < 0) { // Error occurred during receiving
            ESP_LOGE(TAG, "recv failed: errno %d", errno);
            break;
        } else if (len == 0) {
            ESP_LOGI(TAG, "Connection closed");
            break;
        } else { // Data received
            tcp_rx_buf[len] = 0; // Null-terminate whatever we received and treat like a string
            ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);
            //ESP_LOGI(TAG, "%s", tcp_rx_buf);
#if 0
            uint16_t transaction = (*(tcp_rx_buf + MB_TCP_TID) << 8) + *(tcp_rx_buf + MB_TCP_TID + 1);
            uint16_t protocol = (*(tcp_rx_buf + MB_TCP_PID) << 8) + *(tcp_rx_buf + MB_TCP_PID + 1);
            uint16_t tcplen = (*(tcp_rx_buf + MB_TCP_LEN) << 8) + *(tcp_rx_buf + MB_TCP_LEN + 1);
#endif                
            uint8_t slaveId = *(tcp_rx_buf + MB_TCP_UID);
            uint8_t function = *(tcp_rx_buf + MB_TCP_FUNC);
            uint16_t startAddr = (*(tcp_rx_buf + MB_TCP_REGISTER_START) << 8) + *(tcp_rx_buf + MB_TCP_REGISTER_START + 1);
            uint16_t numRegs = (*(tcp_rx_buf + MB_TCP_REGISTER_NUMBER) << 8) + *(tcp_rx_buf + MB_TCP_REGISTER_NUMBER + 1);
#if 0
            ESP_LOGI(TAG, "==========  TCP -> RTU ==========");
            ESP_LOGI(TAG, "Transaction ID: %d", transaction);
            ESP_LOGI(TAG, "Protocol ID: %d", protocol);
            ESP_LOGI(TAG, "Length: %d", tcplen);
            ESP_LOGI(TAG, "Slave ID: %d", slaveId);
            ESP_LOGI(TAG, "Function Code: %d", function);
            ESP_LOGI(TAG, "Start Address: %d (0x%x)", startAddr, startAddr);
            ESP_LOGI(TAG, "Number Registers: %d", numRegs);
#endif
            tcp_tx_buf[0] = *(tcp_rx_buf + MB_TCP_TID);
            tcp_tx_buf[1] = *(tcp_rx_buf + MB_TCP_TID + 1);
            
            tcp_tx_buf[2] = 0; /* Protocol */
            tcp_tx_buf[3] = 0;                    
            
            tcp_tx_buf[4] = 0;
            tcp_tx_buf[5] = (numRegs * 2) + 3; // Number of bytes after this one.
            
            tcp_tx_buf[6] = slaveId;
            tcp_tx_buf[7] = function;
            tcp_tx_buf[8] = numRegs * 2;
            
            len = 9 + (numRegs * 2);
#define PARAM_BUF_SIZE 256
            uint8_t param_buffer[PARAM_BUF_SIZE] = {0};

            esp_err_t err;
            if(len > 255) { /* Overflow !!! */
                err = ESP_ERR_INVALID_ARG;
            } else {
                // Execute modbus request
                mb_param_request_t modbus_request = {
                    slaveId,
                    function,
                    startAddr,
                    numRegs
                };
xSemaphoreTake(mbc_mutex, portMAX_DELAY);
                err = mbc_master_send_request(&modbus_request, &param_buffer[0]);
xSemaphoreGive(mbc_mutex);
            }
            
            if(err == ESP_OK) {
                switch(function)    {
                    case MB_FUNC_READ_INPUT_REGISTER:
                    case MB_FUNC_READ_HOLDING_REGISTERS:
#if 0
                        ESP_LOGI(TAG, "==========  RTU -> TCP ==========");
#endif
                        for(int i=0; i<numRegs; i++) {
#if 0                            
                            ESP_LOGI(TAG, "0x%02x 0x%02x", *(param_buffer + (i * 2)), *(param_buffer + (i * 2) + 1));
#endif
                            tcp_tx_buf[9 + (i * 2)] = *(param_buffer + (i * 2));
                            tcp_tx_buf[9 + (i * 2 + 1)] = *(param_buffer + (i * 2) + 1);
                        }
                        break;
                    /*
                    case MB_FUNC_READ_COILS:
                    case MB_FUNC_WRITE_SINGLE_COIL:
                    case MB_FUNC_WRITE_MULTIPLE_COILS:
                    case MB_FUNC_READ_DISCRETE_INPUTS:
                    
                    case MB_FUNC_WRITE_REGISTER:
                    case MB_FUNC_WRITE_MULTIPLE_REGISTERS:
                    case MB_FUNC_READWRITE_MULTIPLE_REGISTERS:
                    */
                    default:
                        for(int i=0; i<(numRegs * 2); i++) {
                            tcp_tx_buf[9 + i] = *(param_buffer + i);
                        }
                        break;
                }
                //len = 9 + (numRegs * 2);
            } else {
#if 0
                ESP_LOGI(TAG, "==========  RTU -> TCP ========== ERROR %d", err);
#endif
                tcp_tx_buf[5] = 4; // Number of bytes after this one.
                tcp_tx_buf[7] = function + 0x80;
                
                tcp_tx_buf[8] = 1; //Number of bytes after this one (or number of bytes of data).
                tcp_tx_buf[9] = 11; //Error code: Gateway Target Device Failed to Respond

                len = 10;
            }

                //ESP_LOGW(TAG, "Received packet from rtu, len: %d", msg.length);
            int r = send(sock, tcp_tx_buf, len, 0);
            if (r < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending tcp responce: errno %d", errno);
            }
        }
    }

    if (sock != -1) {
        ESP_LOGE(TAG, "Shutting down socket and restarting...");
        shutdown(sock, 0);
        close(sock);
    }

    s_num_tcp_connections--;

    vTaskDelete(NULL);
}

void mbTcp2Serial_task(void *pvParameters)
{
    char addr_str[32];
    int addr_family;
    int ip_protocol;

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(s_tcp_port);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;
    inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0)
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
    else
    	ESP_LOGI(TAG, "Socket created");
#if 0
    /* Set socket option */
    int flag = 1;
    if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int)) < 0) {
        ESP_LOGE(TAG, "setsockopt %d", errno);
    }
#endif
    //setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0)
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
    else
    	ESP_LOGI(TAG, "Socket bound, port %d", s_tcp_port);

    while (1) {
        err = listen(listen_sock, 1);
        if (err != 0) {
            ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
        uint addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            continue;
        }
        ESP_LOGI(TAG, "Socket accepted");

        int nodelay = 1;
        if(setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (void *)&nodelay, sizeof(int)) < 0) {
            ESP_LOGE(TAG, "setsockopt %d", errno);
        }

        // Get the sender's ip address as string
        if(source_addr.sin6_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
        } else if (source_addr.sin6_family == PF_INET6) {
            inet6_ntoa_r(source_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
        }

        tcp_task_t t;
        t.sock = sock;
        strcpy(t.addr_str, addr_str);

        s_num_tcp_connections++;

        xTaskCreatePinnedToCore(&_tcp_task, "_tcp_task", 3072, (void *)&t, 4, NULL, 0);

        while(s_num_tcp_connections >= MAX_TCP_CONNECTIONS) {
            vTaskDelay(100);
        }
    }

    vTaskDelete(NULL);
}
