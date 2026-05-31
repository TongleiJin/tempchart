#ifndef USER_APP_H
#define USER_APP_H

#ifdef __cplusplus
extern "C" {
#endif





void UserApp_Init();
void UserUi_Init();
void UserApp_Start_Init();

// 在头文件中定义
typedef struct {
    float temperature;
    struct {
        uint16_t year;
        uint8_t month;
        uint8_t day;
        uint8_t hour;
        uint8_t minute;
        uint8_t second;
    } ts;
} temp_record_t;





#ifdef __cplusplus
}
#endif

#endif