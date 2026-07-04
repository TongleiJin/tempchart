#include "temp_sampler.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "lite_fifo.h"
#include "port_i2c.h"
#include "port_sht31.h"
#include "port_shtc3.h"

static SemaphoreHandle_t s_read_mutex = NULL;static liteFifo_t s_fifo;
static uint32_t s_sample_count = 0;
static float s_max_temp = 0.0f;
static float s_min_temp = 100.0f;
static size_t s_sensor_source_index = 0;

static const char *s_sensor_source_list[] = {"SHTC3", "SHT31"};
static const size_t s_sensor_source_count =
    sizeof(s_sensor_source_list) / sizeof(s_sensor_source_list[0]);

static bool read_temp_humidity_locked(float *temperature, float *humidity)
{
    if (!temperature || !humidity || !s_read_mutex) {
        return false;
    }

    if (xSemaphoreTake(s_read_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    bool success = false;
    float t = -1000.0f;
    float h = -1000.0f;

    if (s_sensor_source_index == 1) {
        Sht31_ReadTempHumi(&t, &h);
    } else {
        Shtc3_ReadTempHumi(&t, &h);
    }

    if (t != -1000.0f && h != -1000.0f) {
        success = true;
        *temperature = t;
        *humidity = h;
    }

    xSemaphoreGive(s_read_mutex);
    return success;
}

void temp_sampler_init(I2cMasterBus *i2c_bus)
{
    Shtc3_Init(i2c_bus);
    Sht31_Init(i2c_bus);

    s_read_mutex = xSemaphoreCreateMutex();
    assert(s_read_mutex);

    temp_sample_t *buf = (temp_sample_t *)heap_caps_malloc(
        sizeof(temp_sample_t) * TEMP_SAMPLER_MAX_SAMPLES, MALLOC_CAP_SPIRAM);
    assert(buf);
    memset(buf, 0, sizeof(temp_sample_t) * TEMP_SAMPLER_MAX_SAMPLES);
    fifo_CreateLiteFifo(&s_fifo, TEMP_SAMPLER_MAX_SAMPLES, buf);
}

bool temp_sampler_read(float *temperature, float *humidity)
{
    return read_temp_humidity_locked(temperature, humidity);
}

void temp_sampler_push(float temperature, const pcf85063a_datetime_t *timestamp)
{
    if (!timestamp) {
        return;
    }

    temp_sample_t sample = {
        .temperature = temperature,
        .timestamp = *timestamp,
    };

    fifo_PushData(&s_fifo, sample, true);
    s_sample_count++;

    if (temperature > s_max_temp) {
        s_max_temp = temperature;
    }
    if (temperature < s_min_temp) {
        s_min_temp = temperature;
    }
}

void temp_sampler_copy_samples(temp_sample_t *buf, uint16_t len)
{
    if (!buf) {
        return;
    }
    fifo_CopyData(&s_fifo, buf, len);
}

uint32_t temp_sampler_get_count(void)
{
    return s_sample_count;
}

float temp_sampler_get_max(void)
{
    return s_max_temp;
}

float temp_sampler_get_min(void)
{
    return s_min_temp;
}

size_t temp_sampler_get_sensor_source(void)
{
    return s_sensor_source_index;
}

void temp_sampler_set_sensor_source(size_t index)
{
    if (index < s_sensor_source_count) {
        s_sensor_source_index = index;
    }
}

size_t temp_sampler_get_sensor_source_count(void)
{
    return s_sensor_source_count;
}

const char *temp_sampler_get_sensor_source_name(size_t index)
{
    if (index >= s_sensor_source_count) {
        return "";
    }
    return s_sensor_source_list[index];
}
