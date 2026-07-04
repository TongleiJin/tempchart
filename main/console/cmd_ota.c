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
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota_url_store.h"

extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

static const char *TAG = "cmd_ota";
#define OTA_BUF_SIZE 1024

static char s_ota_buf[OTA_BUF_SIZE + 1];
static bool s_ota_running;

static void ota_http_cleanup(esp_http_client_handle_t client)
{
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

static void ota_task(void *param)
{
    char *url = (char *)param;
    esp_err_t err;
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = NULL;
    const esp_partition_t *running = esp_ota_get_running_partition();

    esp_http_client_config_t config = {
        .url = url,
        .cert_pem = (char *)server_cert_pem_start,
        .timeout_ms = CONFIG_APP_OTA_RECV_TIMEOUT,
        .keep_alive_enable = true,
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
        int data_read = esp_http_client_read(client, s_ota_buf, OTA_BUF_SIZE);
        if (data_read < 0) {
            ESP_LOGE(TAG, "SSL read error");
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
                           &s_ota_buf[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)],
                           sizeof(esp_app_desc_t));
                    ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

                    esp_app_desc_t running_app_info;
                    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
                        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
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

            err = esp_ota_write(update_handle, s_ota_buf, data_read);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
                ota_http_cleanup(client);
                esp_ota_abort(update_handle);
                goto done;
            }
            binary_file_length += data_read;
        } else {
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
    s_ota_running = true;
    if (xTaskCreate(ota_task, "ota_task", 8192, url_copy, 5, NULL) != pdPASS) {
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
