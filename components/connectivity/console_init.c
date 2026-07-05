#include "console_init.h"

#include "esp_console.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "ble_app.h"
#include "cmd_system.h"
#include "cmd_nvs.h"
#include "ota_url_store.h"
#include "cmd_board.h"
#include "cmd_sensor.h"
#include "cmd_wifi_conn.h"
#include "cmd_ota.h"
#include "cmd_ping.h"
#include "cmd_ntp.h"
#include "cmd_version.h"

static const char *TAG = "console";
#define PROMPT_STR "tempchart"

#if SOC_USB_SERIAL_JTAG_SUPPORTED
#if !CONFIG_ESP_CONSOLE_SECONDARY_NONE
#warning "Disable secondary console in menuconfig when using esp_console."
#endif
#endif

static void initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void Console_Init(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = PROMPT_STR ">";
    repl_config.max_cmdline_length = CONFIG_CONSOLE_MAX_COMMAND_LINE_LENGTH;

    initialize_nvs();
    ESP_ERROR_CHECK(ota_url_store_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
#if (CONFIG_ESP_WIFI_ENABLED || CONFIG_ESP_HOST_WIFI_ENABLED)
    ESP_ERROR_CHECK(wifi_stack_init());
#endif
    ESP_ERROR_CHECK(ble_app_init());

    esp_console_register_help_command();

    /* Help lists commands in registration order (sorted help disabled).
     * Put rarely used / easy-to-remember commands first; keep wifi and
     * similar commands last so they stay visible above the prompt. */
    register_system_common();
#if SOC_LIGHT_SLEEP_SUPPORTED
    register_system_light_sleep();
#endif
#if SOC_DEEP_SLEEP_SUPPORTED
    register_system_deep_sleep();
#endif
    register_cmd_version();
    register_nvs();
    register_cmd_board();
    register_cmd_sensor();
#if (CONFIG_ESP_WIFI_ENABLED || CONFIG_ESP_HOST_WIFI_ENABLED)
    register_cmd_ping();
    register_cmd_ntp();
#endif
    register_cmd_ota();
#if (CONFIG_ESP_WIFI_ENABLED || CONFIG_ESP_HOST_WIFI_ENABLED)
    register_cmd_wifi();
#endif

#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t hw_config =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));
#elif defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
#else
#error "Unsupported console type"
#endif

    ESP_LOGI(TAG, "Console ready. Type 'help' for commands.");
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
