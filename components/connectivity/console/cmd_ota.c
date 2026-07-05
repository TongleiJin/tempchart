/*
 * OTA console command (from ESP-IDF native_ota_example, triggered on demand)
 */
#include "cmd_ota.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "esp_app_format.h"
#include "esp_console.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "ota_url_store.h"
#include "app_version.h"
#include "port_lvgl.h"
#include "user_app.h"
#include "ble_app.h"
#include "board_config.h"

extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

static const char *TAG = "cmd_ota";
#define OTA_BUF_SIZE BOARD_OTA_BUF_SIZE

static bool s_ota_running;

static void ota_log_heap(const char *label)
{
    printf("%s: internal free=%u, largest=%u; psram free=%u\n",
           label,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static bool ota_tcp_probe(const char *url)
{
    char host[64];
    int port = 0;
    if (!ota_url_parse_host_port(url, host, sizeof(host), &port)) {
        printf("OTA URL parse failed\n");
        return false;
    }

    struct addrinfo hints = {};
    struct addrinfo *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || res == NULL) {
        printf("TCP probe: cannot resolve %s\n", host);
        return false;
    }

    int sock = socket(res->ai_family, res->ai_socktype, 0);
    if (sock < 0) {
        freeaddrinfo(res);
        printf("TCP probe: socket create failed\n");
        return false;
    }

    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int ret = connect(sock, res->ai_addr, res->ai_addrlen);
    close(sock);
    freeaddrinfo(res);

    if (ret != 0) {
        printf("TCP probe failed: cannot reach %s:%d\n", host, port);
        printf("Check: PC server running, same Wi-Fi, firewall port %d, AP isolation\n", port);
        return false;
    }

    printf("TCP probe OK: %s:%d reachable\n", host, port);
    return true;
}

static void ota_prepare_network(void)
{
    esp_wifi_set_ps(WIFI_PS_NONE);
}

static void ota_shutdown_subsystems(void)
{
    printf("Stopping non-OTA subsystems...\n");
    Lvgl_PauseRefresh();
    UserApp_ShutdownForOta();
    Lvgl_ShutdownForOta();
    ble_app_shutdown_for_ota();
    vTaskDelay(pdMS_TO_TICKS(500));
    ota_log_heap("After shutdown");
}

static void ota_http_cleanup(esp_http_client_handle_t client)
{
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

static void ota_task(void *param)
{
    char *url = (char *)param;
    char *ota_buf = heap_caps_malloc(OTA_BUF_SIZE + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    esp_err_t err;
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = NULL;
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (ota_buf == NULL) {
        ESP_LOGE(TAG, "OTA buffer alloc failed");
        goto done;
    }

    ota_log_heap("OTA start");
    ota_prepare_network();

    esp_http_client_config_t config = {
        .url = url,
        .cert_pem = (char *)server_cert_pem_start,
        .timeout_ms = CONFIG_APP_OTA_RECV_TIMEOUT,
        .buffer_size = OTA_BUF_SIZE,
        .keep_alive_enable = false,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP client init failed");
        goto done;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        ota_http_cleanup(client);
        goto done;
    }
    esp_http_client_fetch_headers(client);

    update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found");
        ota_http_cleanup(client);
        goto done;
    }

    ESP_LOGI(TAG, "Writing to %s @ 0x%" PRIx32, update_partition->label, update_partition->address);

    int binary_file_length = 0;
    bool image_header_checked = false;

    while (1) {
        int data_read = esp_http_client_read(client, ota_buf, OTA_BUF_SIZE);
        if (data_read < 0) {
            ESP_LOGE(TAG, "SSL read error, errno=%d", errno);
            ota_http_cleanup(client);
            esp_ota_abort(update_handle);
            goto done;
        }
        if (data_read > 0) {
            if (!image_header_checked) {
                esp_app_desc_t new_app_info;
                if (data_read > (int)(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) +
                                      sizeof(esp_app_desc_t))) {
                    memcpy(&new_app_info,
                           &ota_buf[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)],
                           sizeof(esp_app_desc_t));
                    ESP_LOGI(TAG, "New firmware version: %s (code %lu)",
                             new_app_info.version,
                             (unsigned long)app_version_code_from_string(new_app_info.version));

                    esp_app_desc_t running_app_info;
                    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
                        ESP_LOGI(TAG, "Running firmware version: %s (code %lu)",
                                 running_app_info.version,
                                 (unsigned long)app_version_code_from_string(running_app_info.version));

                        if (app_version_compare_strings(new_app_info.version, running_app_info.version) <= 0) {
                            ESP_LOGW(TAG, "Skip OTA: new firmware is not newer than running");
                            printf("OTA skipped: %s is not newer than running %s\n",
                                   new_app_info.version, running_app_info.version);
                            ota_http_cleanup(client);
                            goto done;
                        }
                    }

                    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
                        ota_http_cleanup(client);
                        goto done;
                    }
                    image_header_checked = true;
                } else {
                    ESP_LOGE(TAG, "First packet too small");
                    ota_http_cleanup(client);
                    goto done;
                }
            }

            err = esp_ota_write(update_handle, ota_buf, data_read);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
                ota_http_cleanup(client);
                esp_ota_abort(update_handle);
                goto done;
            }
            binary_file_length += data_read;
            if ((binary_file_length % (256 * 1024)) < OTA_BUF_SIZE) {
                ESP_LOGI(TAG, "Downloaded %d bytes...", binary_file_length);
            }
        } else if (data_read == 0) {
            if (errno == ECONNRESET || errno == ENOTCONN) {
                break;
            }
            if (esp_http_client_is_complete_data_received(client)) {
                break;
            }
        }
    }

    ESP_LOGI(TAG, "Downloaded %d bytes", binary_file_length);

    if (esp_http_client_is_complete_data_received(client) != true) {
        ESP_LOGE(TAG, "Incomplete download");
        ota_http_cleanup(client);
        esp_ota_abort(update_handle);
        goto done;
    }

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        ota_http_cleanup(client);
        goto done;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        ota_http_cleanup(client);
        goto done;
    }

    ota_http_cleanup(client);
    ESP_LOGI(TAG, "OTA success, restarting...");
    esp_restart();

done:
    printf("OTA failed. Reboot device to restore BLE and UI.\n");
    heap_caps_free(ota_buf);
    free(url);
    s_ota_running = false;
    vTaskDelete(NULL);
}

static int cmd_ota_start(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (s_ota_running) {
        printf("OTA already running\n");
        return 1;
    }

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        printf("Wi-Fi not connected. Run: wifi connect <ssid> <password>\n");
        return 1;
    }

    char url[OTA_URL_MAX_LEN];
    if (ota_url_get(url, sizeof(url)) != ESP_OK) {
        printf("Failed to read OTA URL\n");
        return 1;
    }

    char *url_copy = strdup(url);
    if (url_copy == NULL) {
        printf("Out of memory\n");
        return 1;
    }

    printf("Starting OTA from: %s\n", url_copy);
    ota_prepare_network();
    if (!ota_tcp_probe(url_copy)) {
        free(url_copy);
        return 1;
    }
    ota_shutdown_subsystems();
    s_ota_running = true;
    if (xTaskCreate(ota_task, "ota_task", 12288, url_copy, 5, NULL) != pdPASS) {
        free(url_copy);
        s_ota_running = false;
        printf("Failed to create OTA task\n");
        return 1;
    }
    return 0;
}

static int cmd_ota_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    esp_app_desc_t app_info;

    if (esp_ota_get_partition_description(running, &app_info) == ESP_OK) {
        printf("Running : %s v%s\n", app_info.project_name, app_info.version);
    }
    if (running) {
        printf("Partition: %s @ 0x%lx\n", running->label, (unsigned long)running->address);
    }
    if (next) {
        printf("Next OTA : %s @ 0x%lx\n", next->label, (unsigned long)next->address);
    }

    char url[OTA_URL_MAX_LEN];
    if (ota_url_get(url, sizeof(url)) == ESP_OK) {
        printf("OTA URL  : %s\n", url);
    } else {
        printf("OTA URL  : (unavailable)\n");
    }
    return 0;
}

static int cmd_ota_url_handler(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage:\n");
        printf("  ota_url get\n");
        printf("  ota_url set <ip_or_host>\n");
        printf("  ota_url clear\n");
        return 1;
    }

    if (strcmp(argv[1], "get") == 0) {
        char url[OTA_URL_MAX_LEN];
        if (ota_url_get(url, sizeof(url)) != ESP_OK) {
            printf("Failed to read OTA URL\n");
            return 1;
        }
        printf("OTA URL: %s\n", url);
        return 0;
    }

    if (strcmp(argv[1], "set") == 0) {
        if (argc < 3) {
            printf("Usage: ota_url set <ip_or_host>\n");
            return 1;
        }

        if (ota_url_set_host(argv[2]) != ESP_OK) {
            printf("Failed to save OTA URL\n");
            return 1;
        }

        char url[OTA_URL_MAX_LEN];
        ota_url_get(url, sizeof(url));
        printf("OTA URL saved: %s\n", url);
        return 0;
    }

    if (strcmp(argv[1], "clear") == 0) {
        if (ota_url_clear() != ESP_OK) {
            printf("Failed to clear OTA URL\n");
            return 1;
        }

        char url[OTA_URL_MAX_LEN];
        ota_url_get(url, sizeof(url));
        printf("OTA URL reset to default: %s\n", url);
        return 0;
    }

    printf("Unknown subcommand '%s'\n", argv[1]);
    printf("Usage: ota_url get | set <ip_or_host> | clear\n");
    return 1;
}

void register_cmd_ota(void)
{
    const esp_console_cmd_t start = {
        .command = "ota",
        .help = "Download firmware via HTTPS and reboot (requires Wi-Fi)",
        .hint = NULL,
        .func = &cmd_ota_start,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&start));

    const esp_console_cmd_t info = {
        .command = "ota_info",
        .help = "Show OTA partition and URL info",
        .hint = NULL,
        .func = &cmd_ota_info,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&info));

    const esp_console_cmd_t url = {
        .command = "ota_url",
        .help = "OTA URL in NVS: get | set <ip_or_host> | clear",
        .hint = NULL,
        .func = &cmd_ota_url_handler,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&url));
}
