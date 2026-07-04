#include "user_app.h"
#include "console_init.h"

extern "C" void app_main(void)
{
    TempchartApp_Start();
    Console_Init();
}
