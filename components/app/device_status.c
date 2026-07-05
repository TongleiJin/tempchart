#include "device_status.h"

#include <stdio.h>
#include <string.h>

#include "esp_netif.h"
#include "esp_wifi.h"
#include "firmware_version.h"
#include "ota_url_store.h"
#include "sdkconfig.h"

void device_status_format_system_info(char *buf, size_t size)
{
    if (buf == NULL || size == 0) {
        return;
    }

    buf[0] = '\0';

    snprintf(buf, size, "Ver  : %s", firmware_version);
    snprintf(buf + strlen(buf), size - strlen(buf),
             "\nBuild: %s", firmware_build_month_day_hour());

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        snprintf(buf + strlen(buf), size - strlen(buf),
                 "\nWiFi : %s\nRSSI : %d dBm",
                 (const char *)ap_info.ssid, ap_info.rssi);
    } else {
        snprintf(buf + strlen(buf), size - strlen(buf), "\nWiFi : --");
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL && esp_netif_is_netif_up(netif)) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            snprintf(buf + strlen(buf), size - strlen(buf),
                     "\nIP   : " IPSTR "\nGW   : " IPSTR,
                     IP2STR(&ip_info.ip), IP2STR(&ip_info.gw));
        }
    } else {
        snprintf(buf + strlen(buf), size - strlen(buf), "\nIP   : --");
    }

    snprintf(buf + strlen(buf), size - strlen(buf),
             "\nBLE  : %s", CONFIG_APP_BLE_DEVICE_NAME);

    char ota_url[OTA_URL_MAX_LEN];
    if (ota_url_get(ota_url, sizeof(ota_url)) == ESP_OK) {
        snprintf(buf + strlen(buf), size - strlen(buf), "\nOTA  : %s", ota_url);
    } else {
        snprintf(buf + strlen(buf), size - strlen(buf), "\nOTA  : --");
    }
}
