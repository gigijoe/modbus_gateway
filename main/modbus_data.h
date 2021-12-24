#ifndef _MODBUS_DATA_H
#define _MODBUS_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// This file defines structure of modbus parameters which reflect correspond modbus address space
// for each modbus register type (coils, discreet inputs, holding registers, input registers)
#pragma pack(push, 1)
typedef struct
{
	union {
		struct {
		    uint8_t bit0:1;
		    uint8_t bit1:1;
		    uint8_t bit2:1;
		    uint8_t bit3:1;
		    uint8_t bit4:1;
		    uint8_t bit5:1;
		    uint8_t bit6:1;
		    uint8_t bit7:1;
		};
		struct {
    		uint8_t byte0;
		};
	};
} discrete_reg_params_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct
{
    uint8_t byte0;
} coil_reg_params_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct
{
	uint32_t timestamp;
	uint16_t event; /* Input ON / Input OFF / Output ON / Output OFF / Alarm / System */
	uint16_t index; /* IO index / Alarm type / System event type */	
} syslog_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct
{
	union {
#if 0		
		struct {
			uint16_t word0;
			uint16_t word1;
			uint16_t word2;
			uint16_t word3;
			uint16_t word4;
			uint16_t word5;
			uint16_t word6;
			uint16_t word7;

			uint16_t word8;
			uint16_t word9;
			uint16_t word10;
			uint16_t word11;
			uint16_t word12;
			uint16_t word13;
			uint16_t word14;
			uint16_t word15;
		};
#endif
		struct {
		    float fp0;
		    float fp1;
		    float fp2;
		    float fp3;
		    float fp4;
		    float fp5;
		    float fp6;
		    float fp7;
		};
		struct {
			float voltage0;
			float tempture0;
			float reserve[6];
			uint32_t log_index;
			syslog_t syslog;
		};
	};
} input_reg_params_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct
{
	union {
		struct {
			uint16_t word0;
			uint16_t word1;
			uint16_t word2;
			uint16_t word3;
			uint16_t word4;
			uint16_t word5;
			uint16_t word6;
			uint16_t word7;

			uint16_t word8;
			uint16_t word9;
			uint16_t word10;
			uint16_t word11;
			uint16_t word12;
			uint16_t word13;
			uint16_t word14;
			uint16_t word15;
		};
		struct {
		    float fp0;
		    float fp1;
		    float fp2;
		    float fp3;
		    float fp4;
		    float fp5;
		    float fp6;
		    float fp7;
		};
	};
} holding_reg_params_t;
#pragma pack(pop)

extern holding_reg_params_t holding_reg_params;
extern input_reg_params_t input_reg_params;
extern coil_reg_params_t coil_reg_params;
extern discrete_reg_params_t discrete_reg_params;

#ifdef __cplusplus
}
#endif

#endif