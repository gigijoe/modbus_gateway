#ifndef _MODBUS_TCP2SERIAL_H
#define _MODBUS_TCP2SERIAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void initialize_modbus_tcp2serial();

void mbTcp2Serial_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif