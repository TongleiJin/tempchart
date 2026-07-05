#include "cmd_version.h"

#include <stdio.h>

#include "esp_app_desc.h"
#include "esp_console.h"
#include "esp_system.h"
#include "firmware_version.h"

static int cmd_version_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const esp_app_desc_t *app = esp_app_get_description();
    printf("App     : %s\n", app->project_name);
    printf("Version : %s\n", firmware_version);
    printf("IDF     : %s\n", esp_get_idf_version());
    return 0;
}

void register_cmd_version(void)
{
    const esp_console_cmd_t cmd = {
        .command = "version",
        .help = "Show firmware and SDK version",
        .hint = NULL,
        .func = &cmd_version_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
