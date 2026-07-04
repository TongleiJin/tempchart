/*
 * ICMP ping console command (based on ESP-IDF icmp_echo example).
 */
#include "cmd_ping.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "ping/ping_sock.h"

static const char *TAG = "cmd_ping";
static esp_ping_handle_t s_ping;

static void ping_on_success(esp_ping_handle_t hdl, void *args)
{
    (void)args;

    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;

    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
    printf("%" PRIu32 " bytes from %s icmp_seq=%" PRIu16 " ttl=%" PRIu8 " time=%" PRIu32 " ms\n",
           recv_len, ipaddr_ntoa(&target_addr), seqno, ttl, elapsed_time);
}

static void ping_on_timeout(esp_ping_handle_t hdl, void *args)
{
    (void)args;

    uint16_t seqno;
    ip_addr_t target_addr;

    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    printf("From %s icmp_seq=%" PRIu16 " timeout\n", ipaddr_ntoa(&target_addr), seqno);
}

static void ping_on_end(esp_ping_handle_t hdl, void *args)
{
    (void)args;

    ip_addr_t target_addr;
    uint32_t transmitted;
    uint32_t received;
    uint32_t total_time_ms;
    uint32_t loss;

    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));

    if (transmitted > 0) {
        loss = (uint32_t)((1.0f - ((float)received / (float)transmitted)) * 100.0f);
    } else {
        loss = 0;
    }

#ifdef CONFIG_LWIP_IPV4
    if (IP_IS_V4(&target_addr)) {
        printf("\n--- %s ping statistics ---\n", inet_ntoa(*ip_2_ip4(&target_addr)));
    }
#endif
    printf("%" PRIu32 " packets transmitted, %" PRIu32 " received, %" PRIu32 "%% packet loss, time %" PRIu32 " ms\n",
           transmitted, received, loss, total_time_ms);

    esp_ping_delete_session(hdl);
    s_ping = NULL;
}

static bool ping_resolve_host(const char *host, ip_addr_t *target_addr)
{
    if (ipaddr_aton(host, target_addr)) {
        return true;
    }

    struct addrinfo hint;
    struct addrinfo *res = NULL;

    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_INET;
    if (getaddrinfo(host, NULL, &hint, &res) != 0 || res == NULL) {
        return false;
    }

#ifdef CONFIG_LWIP_IPV4
    if (res->ai_family == AF_INET) {
        struct in_addr addr4 = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
        inet_addr_to_ip4addr(ip_2_ip4(target_addr), &addr4);
        freeaddrinfo(res);
        return true;
    }
#endif

    freeaddrinfo(res);
    return false;
}

static int cmd_ping_handler(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: ping <host> [count]\n");
        return 1;
    }

    if (s_ping != NULL) {
        printf("Ping already running\n");
        return 1;
    }

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        printf("Wi-Fi not connected. Run: wifi connect <ssid> [password]\n");
        return 1;
    }

    ip_addr_t target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    if (!ping_resolve_host(argv[1], &target_addr)) {
        printf("ping: unknown host %s\n", argv[1]);
        return 1;
    }

    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.target_addr = target_addr;
    if (argc >= 3) {
        int count = atoi(argv[2]);
        if (count <= 0) {
            printf("Invalid count: %s\n", argv[2]);
            return 1;
        }
        config.count = (uint32_t)count;
    }

    esp_ping_callbacks_t cbs = {
        .cb_args = NULL,
        .on_ping_success = ping_on_success,
        .on_ping_timeout = ping_on_timeout,
        .on_ping_end = ping_on_end,
    };

    if (esp_ping_new_session(&config, &cbs, &s_ping) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ping_new_session failed");
        s_ping = NULL;
        return 1;
    }

    if (esp_ping_start(s_ping) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ping_start failed");
        esp_ping_delete_session(s_ping);
        s_ping = NULL;
        return 1;
    }

    return 0;
}

void register_cmd_ping(void)
{
    const esp_console_cmd_t ping_cmd = {
        .command = "ping",
        .help = "Send ICMP echo to host: ping <host> [count]",
        .hint = NULL,
        .func = &cmd_ping_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ping_cmd));
}
