#include "wifi_status.h"

#include <stdio.h>
#include <string.h>

#include "esp_netif.h"
#include "esp_wifi.h"

bool wifi_sta_is_connected(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    return netif != NULL && esp_netif_is_netif_up(netif);
}

bool wifi_sta_get_status(wifi_sta_status_t *status)
{
    if (status == NULL) {
        return false;
    }

    memset(status, 0, sizeof(*status));

    if (!wifi_sta_is_connected()) {
        return false;
    }

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        return false;
    }

    status->connected = true;
    strlcpy(status->ssid, (const char *)ap_info.ssid, sizeof(status->ssid));
    status->rssi = ap_info.rssi;

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL && esp_netif_is_netif_up(netif)) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            snprintf(status->ip, sizeof(status->ip), IPSTR, IP2STR(&ip_info.ip));
            snprintf(status->gateway, sizeof(status->gateway), IPSTR, IP2STR(&ip_info.gw));
        }
    }

    return true;
}
