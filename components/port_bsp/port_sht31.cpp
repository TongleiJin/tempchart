#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include "port_sht31.h"
#include "port_i2c.h"

#define TAG "sht31"

static i2c_master_dev_handle_t i2c_master_dev;
static I2cMasterBus *i2cmaster;

// CRC8 (polynomial 0x31) check used by SHT3x series
static bool SHT3x_CheckCrc(uint8_t *data, uint8_t len, uint8_t checksum)
{
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x31;
            else crc <<= 1;
        }
    }
    return (crc == checksum);
}

void Sht31_Init(I2cMasterBus *i2cmasterbus)
{
    i2cmaster = i2cmasterbus;
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = 0x44; // SHT31 default address
    dev_cfg.scl_speed_hz = 100000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2cmasterbus->Get_I2cBusHandle(), &dev_cfg, &i2c_master_dev));
    // Soft reset (0x30A2)
    uint8_t resetCmd[2] = {0x30, 0xA2};
    i2cmaster->i2c_write_buff(i2c_master_dev, -1, resetCmd, 2);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "SHT31 initialized at 0x44");
}

void Sht31_ReadTempHumi(float *t, float *h)
{
    // Single shot, high repeatability, no clock stretching: 0x2400
    uint8_t cmd[2] = {0x24, 0x00};
    uint8_t readBuf[6] = {0};
    int err = i2cmaster->i2c_write_buff(i2c_master_dev, -1, cmd, 2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WRITE Failure: %d", err);
        *t = -1000; *h = -1000; return;
    }
    // Typical measurement duration for high repeatability ~15ms
    vTaskDelay(pdMS_TO_TICKS(20));
    err = i2cmaster->i2c_read_buff(i2c_master_dev, -1, readBuf, 6);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "READ Failure: %d", err);
        *t = -1000; *h = -1000; return;
    }
    // check CRC for temperature and humidity
    if (!SHT3x_CheckCrc(&readBuf[0], 2, readBuf[2])) {
        ESP_LOGE(TAG, "Temp CRC Failure"); *t = -1000; *h = -1000; return;
    }
    if (!SHT3x_CheckCrc(&readBuf[3], 2, readBuf[5])) {
        ESP_LOGE(TAG, "Humi CRC Failure"); *t = -1000; *h = -1000; return;
    }
    uint16_t rawT = (readBuf[0] << 8) | readBuf[1];
    uint16_t rawH = (readBuf[3] << 8) | readBuf[4];
    *t = -45.0f + 175.0f * (float)rawT / 65535.0f;
    *h = 100.0f * (float)rawH / 65535.0f;
}
