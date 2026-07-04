/*
 * Wi-Fi console commands for HTTPS OTA (STA connect only).
 * Initialized once at boot; avoids double-init with esp_netif/event loop.
 */
#include "cmd_wifi_conn.h"

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "wifi_status.h"

static const char *TAG = "cmd_wifi";
#define WIFI_JOIN_TIMEOUT_MS 15000

static EventGroupHandle_t s_wifi_event_group;
static const int CONNECTED_BIT = BIT0;
static bool s_wifi_stack_ready;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
    }
}

esp_err_t wifi_stack_init(void)
{
    if (s_wifi_stack_ready) {
        return ESP_OK;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_wifi_stack_ready = true;
    ESP_LOGI(TAG, "Wi-Fi stack ready (STA mode)");
    return ESP_OK;
}

static bool wifi_connect_to(const char *ssid, const char *pass, int timeout_ms)
{
    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (pass != NULL && pass[0] != '\0') {
        strlcpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
    ESP_ERROR_CHECK(esp_wifi_connect());

    int bits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT,
                                   pdFALSE, pdTRUE,
                                   timeout_ms / portTICK_PERIOD_MS);
    return (bits & CONNECTED_BIT) != 0;
}

static void print_wifi_ip(void)
{
    wifi_sta_status_t status;
    if (!wifi_sta_get_status(&status)) {
        printf("Wi-Fi IP: not connected\n");
        return;
    }

    if (status.ip[0] == '\0') {
        printf("Wi-Fi IP: address not assigned\n");
        return;
    }

    printf("IP      : %s\n", status.ip);
    if (status.gateway[0] != '\0') {
        printf("Gateway : %s\n", status.gateway);
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL) {
        esp_netif_dns_info_t dns;
        if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK &&
            dns.ip.u_addr.ip4.addr != 0) {
            printf("DNS     : " IPSTR "\n", IP2STR(&dns.ip.u_addr.ip4));
        }
    }
}

static int cmd_wifi_handler(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage:\n");
        printf("  wifi connect <ssid> [password]\n");
        printf("  wifi status\n");
        printf("  wifi ip\n");
        return 1;
    }

    if (strcmp(argv[1], "connect") == 0) {
        if (argc < 3) {
            printf("Usage: wifi connect <ssid> [password]\n");
            return 1;
        }

        const char *ssid = argv[2];
        const char *pass = (argc >= 4) ? argv[3] : NULL;

        printf("Connecting to '%s'...\n", ssid);
        if (!wifi_connect_to(ssid, pass, WIFI_JOIN_TIMEOUT_MS)) {
            printf("Connection timed out\n");
            return 1;
        }

        wifi_sta_status_t status;
        if (wifi_sta_get_status(&status)) {
            printf("Connected to '%s' (RSSI %d)\n", status.ssid, status.rssi);
        } else {
            printf("Connected\n");
        }
        return 0;
    }

    if (strcmp(argv[1], "status") == 0) {
        wifi_sta_status_t status;
        if (!wifi_sta_get_status(&status)) {
            printf("Wi-Fi: not connected\n");
            return 0;
        }
        printf("Wi-Fi: connected to '%s' (RSSI %d)\n", status.ssid, status.rssi);
        return 0;
    }

    if (strcmp(argv[1], "ip") == 0) {
        print_wifi_ip();
        return 0;
    }

    printf("Unknown subcommand '%s'\n", argv[1]);
    printf("Usage: wifi connect <ssid> [password] | wifi status | wifi ip\n");
    return 1;
}

void register_cmd_wifi(void)
{
    const esp_console_cmd_t wifi_cmd = {
        .command = "wifi",
        .help = "Wi-Fi commands: connect | status | ip",
        .hint = NULL,
        .func = &cmd_wifi_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_cmd));
}
