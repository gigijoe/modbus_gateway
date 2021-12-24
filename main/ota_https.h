#ifndef _OTA_HTTPS_HPP
#define _OTA_HTTPS_HPP

#ifdef __cplusplus
extern "C" {
#endif

/*
* https://github.com/iot-lorawan/esp32-ota-ble
*/

void ota_https_run(const char *host, uint16_t port, const char *firmware);

#ifdef __cplusplus
}
#endif

#endif
