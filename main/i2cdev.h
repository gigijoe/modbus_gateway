#ifndef MAIN_I2CDEV_H_
#define MAIN_I2CDEV_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "driver/i2c.h"
#include "freertos/semphr.h"

#define I2C_FREQ_HZ 100000
#define I2CDEV_TIMEOUT 1000

extern SemaphoreHandle_t i2cMutex;

typedef struct {
	i2c_port_t port;            // I2C port number
	uint8_t addr;               // I2C address
	gpio_num_t sda_io_num;      // GPIO number for I2C sda signal
	gpio_num_t scl_io_num;      // GPIO number for I2C scl signal
	uint32_t clk_speed;             // I2C clock frequency for master mode
} i2c_dev_t;

esp_err_t i2c_master_init(i2c_port_t port, int sda, int scl);
esp_err_t i2c_dev_read(const i2c_dev_t *dev, const void *out_data, size_t out_size, void *in_data, size_t in_size);
esp_err_t i2c_dev_write(const i2c_dev_t *dev, const void *out_reg, size_t out_reg_size, const void *out_data, size_t out_size);
esp_err_t i2c_dev_read_reg(const i2c_dev_t *dev, uint8_t reg,
        void *in_data, size_t in_size);
esp_err_t i2c_dev_write_reg(const i2c_dev_t *dev, uint8_t reg,
        const void *out_data, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_I2CDEV_H_ */
