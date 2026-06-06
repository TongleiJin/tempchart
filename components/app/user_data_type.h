/*
 * user_data_type.h
 * define the user data type, ONLY
 */

#ifndef __USER_DATA_H__
#define __USER_DATA_H__

#include "pcf85063a.h"

typedef struct _user_data
{
    float temperature;
    pcf85063a_datetime_t timestamp;

} user_data_t;



#endif /* __USER_DATA_H__ */
