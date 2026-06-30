#ifndef USER_APP_H
#define USER_APP_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


void UserApp_Init();
void UserUi_Init();
void UserApp_Start_Init();
bool UserApp_ReadTempHumidity(float *temperature, float *humidity);
void UserApp_GetTimeStr(char *buf, size_t len);


#ifdef __cplusplus
}
#endif

#endif