/*
 * On-demand SNTP sync to external RTC (loosely coupled via port_rtc).
 */
#include "cmd_ntp.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_console.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "port_rtc.h"

static const char *TAG = "cmd_ntp";

static esp_err_t sync_time_from_network(struct tm *out_local_time)
{
    setenv("TZ", CONFIG_APP_NTP_TIMEZONE, 1);
    tzset();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_APP_NTP_SERVER);
    esp_err_t ret = esp_netif_sntp_init(&config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_netif_sntp_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(CONFIG_APP_NTP_SYNC_TIMEOUT));
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SNTP sync failed: %s", esp_err_to_name(ret));
        esp_netif_sntp_deinit();
        return ret;
    }

    time_t now = 0;
    time(&now);
    localtime_r(&now, out_local_time);

    esp_netif_sntp_deinit();
    return ESP_OK;
}

esp_err_t ntp_sync_rtc(void)
{
    if (!PortRtc_IsReady())
    {
        return ESP_ERR_INVALID_STATE;
    }

    struct tm network_time = {};
    esp_err_t ret = sync_time_from_network(&network_time);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return PortRtc_SetLocalTime(&network_time);
}

void ntp_format_rtc_time(char *buf, size_t len)
{
    if (buf == NULL || len == 0)
    {
        return;
    }

    if (!PortRtc_IsReady())
    {
        snprintf(buf, len, "RTC: N/A");
        return;
    }

    struct tm rtc_time = {};
    if (PortRtc_GetLocalTime(&rtc_time) != ESP_OK)
    {
        snprintf(buf, len, "RTC: err");
        return;
    }

    strftime(buf, len, "RTC:%m-%d %H:%M:%S", &rtc_time);
}

static void print_local_time(const char *label, const struct tm *time)
{
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", time);
    printf("%s: %s\n", label, buf);
}

static int cmd_ntp_handler(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage:\n");
        printf("  ntp sync   Sync RTC from NTP server\n");
        printf("  ntp show   Show current RTC time\n");
        return 1;
    }

    if (strcmp(argv[1], "show") == 0)
    {
        if (!PortRtc_IsReady())
        {
            printf("RTC not available\n");
            return 1;
        }

        struct tm rtc_time = {};
        if (PortRtc_GetLocalTime(&rtc_time) != ESP_OK)
        {
            printf("Failed to read RTC\n");
            return 1;
        }

        print_local_time("RTC", &rtc_time);
        return 0;
    }

    if (strcmp(argv[1], "sync") != 0)
    {
        printf("Unknown subcommand '%s'\n", argv[1]);
        printf("Usage: ntp sync | ntp show\n");
        return 1;
    }

    if (!PortRtc_IsReady())
    {
        printf("RTC not available\n");
        return 1;
    }

    printf("Syncing from NTP server: %s\n", CONFIG_APP_NTP_SERVER);

    struct tm network_time = {};
    if (sync_time_from_network(&network_time) != ESP_OK)
    {
        printf("NTP sync failed\n");
        return 1;
    }

    if (PortRtc_SetLocalTime(&network_time) != ESP_OK)
    {
        printf("Failed to write RTC\n");
        return 1;
    }

    print_local_time("RTC updated", &network_time);
    return 0;
}

void register_cmd_ntp(void)
{
    const esp_console_cmd_t ntp_cmd = {
        .command = "ntp",
        .help = "Network time: sync (NTP -> RTC) | show",
        .hint = NULL,
        .func = &cmd_ntp_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ntp_cmd));
}
