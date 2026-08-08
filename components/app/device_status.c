#include "device_status.h"

#include <stdio.h>
#include <string.h>

#include "esp_netif.h"
#include "esp_wifi.h"
#include "firmware_version.h"
#include "ota_url_store.h"
#include "sdkconfig.h"
#include "input_handler.h"
#include "temp_sampler.h"
#include "chart_controller.h"
#include "port_rtc.h"

void device_status_format_system_info1(char *buf, size_t size)
{
    buf[0] = '\0';
    uint32_t active_s = input_handler_get_active_sample_period_ms() / 1000;
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "Sample period: %lu", active_s);
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nCount: %lu", temp_sampler_get_count());
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nTemp offset: %u", chart_controller_get_temp_offset());
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nTemp scaler: %u", chart_controller_get_temp_scaler());
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nChart size: %lu", chart_controller_get_point_count());
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nSensor Src: %s",
             temp_sampler_get_sensor_source_name(temp_sampler_get_sensor_source()));

    float t = 0.0f;
    float h = 0.0f;
    if (temp_sampler_read(&t, &h))
    {
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nTemp: %.1f°C\nHum: %.1f%%", t, h);
    }
    else
    {
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nTemp: --°C\nHum: --%%");
    }
}


void device_status_format_system_info(char *buf, size_t size)
{
    struct tm rtc_time = {};

    if (buf == NULL || size == 0)
    {
        return;
    }

    buf[0] = '\0';
    PortRtc_GetLocalTime(&rtc_time);

    snprintf(buf, size, "Ver  : %s", firmware_version);
    // append current date
    snprintf(buf + strlen(buf), size - strlen(buf),
             "\nDate : %02d-%02d-%04d", rtc_time.tm_mon + 1, rtc_time.tm_mday, rtc_time.tm_year + 1900);
    // append current time
    snprintf(buf + strlen(buf), size - strlen(buf),
             "\nTime : %02d:%02d:%02d", rtc_time.tm_hour, rtc_time.tm_min, rtc_time.tm_sec);

    snprintf(buf + strlen(buf), size - strlen(buf),
             "\nBuild: %s", firmware_build_month_day_hour());

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
    {
        snprintf(buf + strlen(buf), size - strlen(buf),
                 "\nWiFi : %s\nRSSI : %d dBm",
                 (const char *)ap_info.ssid, ap_info.rssi);
    }
    else
    {
        snprintf(buf + strlen(buf), size - strlen(buf), "\nWiFi : --");
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL && esp_netif_is_netif_up(netif))
    {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK)
        {
            snprintf(buf + strlen(buf), size - strlen(buf),
                     "\nIP   : " IPSTR "\nGW   : " IPSTR,
                     IP2STR(&ip_info.ip), IP2STR(&ip_info.gw));
        }
    }
    else
    {
        snprintf(buf + strlen(buf), size - strlen(buf), "\nIP   : --");
    }

    snprintf(buf + strlen(buf), size - strlen(buf),
             "\nBLE  : %s", CONFIG_APP_BLE_DEVICE_NAME);

    char ota_url[OTA_URL_MAX_LEN];
    if (ota_url_get(ota_url, sizeof(ota_url)) == ESP_OK)
    {
        snprintf(buf + strlen(buf), size - strlen(buf), "\nOTA  : %s", ota_url);
    }
    else
    {
        snprintf(buf + strlen(buf), size - strlen(buf), "\nOTA  : --");
    }
}
