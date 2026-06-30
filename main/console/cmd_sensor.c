#include "cmd_sensor.h"

#include <stdio.h>

#include "esp_console.h"
#include "port_adc.h"
#include "user_app.h"

static int cmd_temp(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    float temperature = 0.0f;
    float humidity = 0.0f;
    if (!UserApp_ReadTempHumidity(&temperature, &humidity)) {
        printf("Failed to read temperature and humidity\n");
        return 1;
    }
    printf("Temperature: %.2f C\n", temperature);
    printf("Humidity   : %.2f %%\n", humidity);
    return 0;
}

static int cmd_battery(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    float voltage = Get_VbatVoltage();
    uint8_t level = Get_Batterylevel();
    printf("Battery: %.2f V (%u%%)\n", voltage, level);
    return 0;
}

void register_cmd_sensor(void)
{
    const esp_console_cmd_t temp = {
        .command = "temp",
        .help = "Read current temperature and humidity (SHTC3)",
        .hint = NULL,
        .func = &cmd_temp,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&temp));

    const esp_console_cmd_t battery = {
        .command = "battery",
        .help = "Read battery voltage and level",
        .hint = NULL,
        .func = &cmd_battery,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&battery));
}
