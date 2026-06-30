#ifndef USER_APP_H
#define USER_APP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


void UserApp_Init();
void UserUi_Init();
void UserApp_Start_Init();
bool UserApp_ReadTempHumidity(float *temperature, float *humidity);


#ifdef __cplusplus
}
#endif

#endif