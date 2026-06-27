#ifndef USER_APP_H
#define USER_APP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif





bool UserApp_ReadTempHumi(float *temperature, float *humidity);

void UserApp_Init();
void UserUi_Init();
void UserApp_Start_Init();




#ifdef __cplusplus
}
#endif

#endif