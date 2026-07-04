#include "cmd_board.h"

#include <stdio.h>
#include <string.h>

#include "epaper_config.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "port_power.h"

static int cmd_board_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();

    printf("Board : Waveshare ESP32-S3-Touch-ePaper-1.54\n");
    printf("Display: %dx%d e-paper\n", EPD_WIDTH, EPD_HEIGHT);
    printf("Cores : %d @ %dMHz\n", chip.cores, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    printf("App   : %s v%s\n", app->project_name, app->version);
    if (running) {
        printf("Part  : %s @ 0x%lx\n", running->label, (unsigned long)running->address);
    }
    printf("EPD   : pwr=%d busy=%d rst=%d dc=%d cs=%d clk=%d mosi=%d\n",
           EPD_PWR_PIN, EPD_BUSY_PIN, EPD_RST_PIN,
           EPD_DC_PIN, EPD_CS_PIN, EPD_SCK_PIN, EPD_MOSI_PIN);
    printf("I2C   : scl=%d sda=%d\n", ESP32_I2C_SCL_PIN, ESP32_I2C_SDA_PIN);
    printf("Touch : rst=%d int=%d\n", EPD_TP_RST_PIN, EPD_TP_INT_PIN);
    return 0;
}

static int cmd_epd_power(int argc, char **argv)
{
    if (argc != 2) {
        printf("Usage: epd_power <on|off>\n");
        return 1;
    }

    if (strcmp(argv[1], "on") == 0) {
        BoardPower_EPD_ON();
        printf("E-Paper power ON\n");
    } else if (strcmp(argv[1], "off") == 0) {
        BoardPower_EPD_OFF();
        printf("E-Paper power OFF\n");
    } else {
        printf("Usage: epd_power <on|off>\n");
        return 1;
    }
    return 0;
}

void register_cmd_board(void)
{
    const esp_console_cmd_t info = {
        .command = "board",
        .help = "Show board and firmware info",
        .hint = NULL,
        .func = &cmd_board_info,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&info));

    const esp_console_cmd_t epd = {
        .command = "epd_power",
        .help = "Enable or disable E-Paper 3.3V power (GPIO6)",
        .hint = NULL,
        .func = &cmd_epd_power,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&epd));
}
