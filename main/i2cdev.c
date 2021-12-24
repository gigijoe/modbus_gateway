#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c.h"
#include "esp_log.h"

#include "i2cdev.h"

#define TAG "I2CDEV"

xSemaphoreHandle i2cMutex;

static bool s_initialized = false;

esp_err_t i2c_master_init(i2c_port_t port, int sda, int scl)
{
    if(s_initialized)
        return ESP_OK;

    i2cMutex = xSemaphoreCreateMutex();

    i2c_config_t i2c_config = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = sda,
            .scl_io_num = scl,
            .sda_pullup_en = GPIO_PULLUP_ENABLE,
            .scl_pullup_en = GPIO_PULLUP_ENABLE,
            .master.clk_speed = I2C_FREQ_HZ
    };
    i2c_param_config(port, &i2c_config);
    esp_err_t r = i2c_driver_install(port, I2C_MODE_MASTER, 0, 0, 0);

    if(r == ESP_OK)
        s_initialized = true;

    return r;
}

esp_err_t i2c_dev_read(const i2c_dev_t *dev, const void *out_data, size_t out_size, void *in_data, size_t in_size)
{
    if (!dev || !in_data || !in_size) return ESP_ERR_INVALID_ARG;

xSemaphoreTake(i2cMutex, portMAX_DELAY);

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (out_data && out_size)
    {
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (dev->addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write(cmd, (void *)out_data, out_size, true);
    }
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->addr << 1) | 1, true);
    i2c_master_read(cmd, in_data, in_size, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);

    esp_err_t res = i2c_master_cmd_begin(dev->port, cmd, I2CDEV_TIMEOUT / portTICK_RATE_MS);
    if (res != ESP_OK)
        ESP_LOGE(TAG, "Could not read from device [0x%02x at %d]: %d", dev->addr, dev->port, res);
    i2c_cmd_link_delete(cmd);

xSemaphoreGive(i2cMutex);

    return res;
}

esp_err_t i2c_dev_write(const i2c_dev_t *dev, const void *out_reg, size_t out_reg_size, const void *out_data, size_t out_size)
{
    if (!dev || !out_data || !out_size) return ESP_ERR_INVALID_ARG;

xSemaphoreTake(i2cMutex, portMAX_DELAY);

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev->addr << 1) | I2C_MASTER_WRITE, true);
    if (out_reg && out_reg_size)
        i2c_master_write(cmd, (void *)out_reg, out_reg_size, true);
    i2c_master_write(cmd, (void *)out_data, out_size, true);
    i2c_master_stop(cmd);
    esp_err_t res = i2c_master_cmd_begin(dev->port, cmd, I2CDEV_TIMEOUT / portTICK_RATE_MS);
    if (res != ESP_OK)
        ESP_LOGE(TAG, "Could not write to device [0x%02x at %d]: %d", dev->addr, dev->port, res);
    i2c_cmd_link_delete(cmd);

xSemaphoreGive(i2cMutex);

    return res;
}

inline esp_err_t i2c_dev_read_reg(const i2c_dev_t *dev, uint8_t reg,
        void *in_data, size_t in_size)
{
    return i2c_dev_read(dev, &reg, 1, in_data, in_size);
}

inline esp_err_t i2c_dev_write_reg(const i2c_dev_t *dev, uint8_t reg,
        const void *out_data, size_t out_size)
{
    return i2c_dev_write(dev, &reg, 1, out_data, out_size);
}