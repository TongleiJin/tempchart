// Minimal SHT31 driver for single-shot high repeatability reads
#pragma once
#include "port_i2c.h"

void Sht31_Init(I2cMasterBus *i2cmasterbus);
void Sht31_ReadTempHumi(float *t, float *h);
