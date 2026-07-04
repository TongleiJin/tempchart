/*
 * temp_sample.h
 * Temperature sample record with timestamp.
 */

#ifndef TEMP_SAMPLE_H
#define TEMP_SAMPLE_H

#include "pcf85063a.h"

typedef struct
{
    float temperature;
    pcf85063a_datetime_t timestamp;

} temp_sample_t;

#endif /* TEMP_SAMPLE_H */
