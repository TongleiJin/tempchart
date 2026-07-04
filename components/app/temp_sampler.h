#ifndef TEMP_SAMPLER_H
#define TEMP_SAMPLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "temp_sample.h"

#ifdef __cplusplus
class I2cMasterBus;
extern "C" {
#else
typedef struct I2cMasterBus I2cMasterBus;
#endif

#define TEMP_SAMPLER_MAX_SAMPLES 100

void temp_sampler_init(I2cMasterBus *i2c_bus);
bool temp_sampler_read(float *temperature, float *humidity);
void temp_sampler_push(float temperature, const pcf85063a_datetime_t *timestamp);
void temp_sampler_copy_samples(temp_sample_t *buf, uint16_t len);

uint32_t temp_sampler_get_count(void);
float temp_sampler_get_max(void);
float temp_sampler_get_min(void);

size_t temp_sampler_get_sensor_source(void);
void temp_sampler_set_sensor_source(size_t index);
size_t temp_sampler_get_sensor_source_count(void);
const char *temp_sampler_get_sensor_source_name(size_t index);

#ifdef __cplusplus
}
#endif

#endif /* TEMP_SAMPLER_H */
