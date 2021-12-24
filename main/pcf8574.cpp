/*
 * See header file for details
 *
 *  This program is free software: you can redistribute it and/or modify\n
 *  it under the terms of the GNU General Public License as published by\n
 *  the Free Software Foundation, either version 3 of the License, or\n
 *  (at your option) any later version.\n
 * 
 *  This program is distributed in the hope that it will be useful,\n
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of\n
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n
 *  GNU General Public License for more details.\n
 * 
 *  You should have received a copy of the GNU General Public License\n
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.\n
 */

/* Dependencies */
#include "pcf8574.hpp"

#include <driver/gpio.h>
#include <driver/i2c.h>

#include "i2cdev.h"

PCF8574::PCF8574() :
		_PORT(0), _PIN(0), _DDR(0), _address(0)
{
}

uint8_t PCF8574::readRegister()
{
xSemaphoreTake(i2cMutex, portMAX_DELAY);

	int ret;
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (_address) | I2C_MASTER_READ, 1 /* expect ack */);
	uint8_t tmpByte;
	i2c_master_read_byte(cmd, &tmpByte, (i2c_ack_type_t)1);
	i2c_master_stop(cmd);
	ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 100 / portTICK_RATE_MS);
	if (ret != ESP_OK)
	  printf("I2C : %d - %s:%d\n", ret, __PRETTY_FUNCTION__, __LINE__); 
	i2c_cmd_link_delete(cmd);

xSemaphoreGive(i2cMutex);

	return tmpByte;
}

void PCF8574::writeRegister(uint8_t regValue)
{
xSemaphoreTake(i2cMutex, portMAX_DELAY);

	int ret;
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (_address) | I2C_MASTER_WRITE, 1 /* expect ack */);
	i2c_master_write_byte(cmd, regValue, 1);
	i2c_master_stop(cmd);
	ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 100 / portTICK_RATE_MS);
	if (ret != ESP_OK)
	  printf("I2C : %d - %s:%d\n", ret, __PRETTY_FUNCTION__, __LINE__);   
	i2c_cmd_link_delete(cmd);

xSemaphoreGive(i2cMutex);
}

void PCF8574::begin(uint8_t address) {

	/* Store the I2C address and init the Wire library */
	_address = address;
	//Wire.begin();
	//readGPIO();
	readRegister();
}

void PCF8574::pinMode(uint8_t pin, uint8_t mode) {

	/* Switch according mode */
	switch (mode) {
	case INPUT:
		_DDR &= ~(1 << pin);
		_PORT &= ~(1 << pin);
		break;

	case INPUT_PULLUP:
		_DDR &= ~(1 << pin);
		_PORT |= (1 << pin);
		break;

	case OUTPUT:
		_DDR |= (1 << pin);
		_PORT &= ~(1 << pin);		
		break;

	case OUTPUT_PULLUP:
		_DDR |= (1 << pin);
		_PORT |= (1 << pin);		
		break;

	default:
		break;
	}

	/* Update GPIO values */
	updateGPIO();
}

void PCF8574::allPinsMode(uint8_t mode){
	switch (mode) {
	case INPUT:
		_DDR = 0x00;
		_PORT = 0x00;
		break;

	case INPUT_PULLUP:
		_DDR = 0x00;
		_PORT = 0xff;
		break;

	case OUTPUT:
		_DDR = 0xff;
		_PORT = 0x00;		
		break;

	case OUTPUT_PULLUP:
		_DDR = 0xff;
		_PORT = 0xff;		
		break;

	default:
		break;
	}

	/* Update GPIO values */
	updateGPIO();
}

void PCF8574::digitalWrite(uint8_t pin, uint8_t value) {

	/* Set PORT bit value */
	if (value)
		_PORT |= (1 << pin);
	else
		_PORT &= ~(1 << pin);

	/* Update GPIO values */
	updateGPIO();
}

uint8_t PCF8574::digitalRead(uint8_t pin) {

	/* Read GPIO */
	readGPIO();

	/* Read and return the pin state */
	return (_PIN & (1 << pin)) ? 1 : 0;
}

void PCF8574::write(uint8_t value) {

	/* Store pins values and apply */
	_PORT = value;

	/* Update GPIO values */
	updateGPIO();
}

uint8_t PCF8574::read() {

	/* Read GPIO */
	readGPIO();

	/* Return current pins values */
	return _PIN;
}

void PCF8574::pullUp(uint8_t pin) {

	/* Same as pinMode(INPUT_PULLUP) */
	pinMode(pin, INPUT_PULLUP); // /!\ pinMode form THE LIBRARY
}

void PCF8574::pullDown(uint8_t pin) {

	/* Same as pinMode(INPUT) */
	pinMode(pin, INPUT); // /!\ pinMode form THE LIBRARY
}

void PCF8574::clear() {

	/* User friendly wrapper for write() */
	write(0x00);
}

void PCF8574::set() {

	/* User friendly wrapper for write() */
	write(0xFF);
}

void PCF8574::toggle(uint8_t pin) {

	/* Toggle pin state */
	_PORT ^= (1 << pin);

	/* Update GPIO values */
	updateGPIO();
}

void PCF8574::readGPIO() {
	/* Start request, wait for data and receive GPIO values as byte */
	//Wire.requestFrom(_address, (uint8_t) 0x01);
	//while (Wire.available() < 1)
	//	;
	//_PIN = I2CREAD();
	_PIN = readRegister();
}

void PCF8574::updateGPIO() {

	/* Read current GPIO states */
	//readGPIO(); // Experimental

	/* Compute new GPIO states */
	//uint8_t value = ((_PIN & ~_DDR) & ~(~_DDR & _PORT)) | _PORT; // Experimental
	uint8_t value = (_PIN & ~_DDR) | _PORT;

	/* Start communication and send GPIO values as byte */
	//Wire.beginTransmission(_address);
	//I2CWRITE(value);
	//Wire.endTransmission();
	writeRegister(value);
}
