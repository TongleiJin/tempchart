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
#include "firmware_version.h"
#include "port_lvgl.h"
#include "user_app.h"
#include "ble_app.h"
#include "board_config.h"
#include "cmd_wifi_conn.h"
#include "esp_timer.h"

extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

static const char *TAG = "cmd_ota";
#define OTA_BUF_SIZE BOARD_OTA_BUF_SIZE
#define OTA_STUCK_TIMEOUT_MS 30000

static bool s_ota_running;
static bool s_ota_ui_mode;
static ota_ui_status_t s_ota_status;
static int64_t s_ota_last_progress_us;

static void ota_set_state(ota_ui_state_t state)
{
    s_ota_status.state = state;
}

static void ota_set_progress(int bytes, int content_length)
{
    s_ota_status.progress_bytes = bytes;
    s_ota_status.content_length = content_length;
    s_ota_last_progress_us = esp_timer_get_time();
}

static void ota_log_heap(const char *label)
{
    printf("%s: internal free=%u, largest=%u; psram free=%u\n",
           label,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static bool ota_parse_host_port(const char *url, char *host, size_t host_len, int *port)
{
    const char *p = url;
    if (strncmp(p, "https://", 8) == 0)
    {
        p += 8;
    }
    else if (strncmp(p, "http://", 7) == 0)
    {
        p += 7;
    }

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    size_t len;

    if (colon != NULL && (slash == NULL || colon < slash))
    {
        len = (size_t)(colon - p);
        if (len == 0 || len >= host_len)
        {
            return false;
        }
        memcpy(host, p, len);
        host[len] = '\0';
        *port = atoi(colon + 1);
    }
    else
    {
        len = slash ? (size_t)(slash - p) : strlen(p);
        if (len == 0 || len >= host_len)
        {
            return false;
        }
        memcpy(host, p, len);
        host[len] = '\0';
        *port = 443;
    }
    return *port > 0;
}

static bool ota_tcp_probe(const char *url)
{
    char host[64];
    int port = 0;
    if (!ota_parse_host_port(url, host, sizeof(host), &port))
    {
        printf("OTA URL parse failed\n");
        return false;
    }

    struct addrinfo hints = {};
    struct addrinfo *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || res == NULL)
    {
        printf("TCP probe: cannot resolve %s\n", host);
        return false;
    }

    int sock = socket(res->ai_family, res->ai_socktype, 0);
    if (sock < 0)
    {
        freeaddrinfo(res);
        printf("TCP probe: socket create failed\n");
        return false;
    }

    struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int ret = connect(sock, res->ai_addr, res->ai_addrlen);
    close(sock);
    freeaddrinfo(res);

    if (ret != 0)
    {
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
    bool ui_mode = s_ota_ui_mode;

    if (ota_buf == NULL)
    {
        ESP_LOGE(TAG, "OTA buffer alloc failed");
        ota_set_state(OTA_UI_FAILED);
        goto done;
    }

    ota_log_heap("OTA start");
    ota_prepare_network();
    ota_set_state(OTA_UI_DOWNLOADING);
    ota_set_progress(0, -1);

    esp_http_client_config_t config = {
        .url = url,
        .cert_pem = (char *)server_cert_pem_start,
        .timeout_ms = CONFIG_APP_OTA_RECV_TIMEOUT,
        .buffer_size = OTA_BUF_SIZE,
        .keep_alive_enable = false,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        ESP_LOGE(TAG, "HTTP client init failed");
        ota_set_state(OTA_UI_FAILED);
        goto done;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        ota_http_cleanup(client);
        ota_set_state(OTA_UI_FAILED);
        goto done;
    }
    esp_http_client_fetch_headers(client);
    ota_set_progress(0, esp_http_client_get_content_length(client));

    update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL)
    {
        ESP_LOGE(TAG, "No OTA partition found");
        ota_http_cleanup(client);
        ota_set_state(OTA_UI_FAILED);
        goto done;
    }

    ESP_LOGI(TAG, "Writing to %s @ 0x%" PRIx32, update_partition->label, update_partition->address);

    int binary_file_length = 0;
    bool image_header_checked = false;

    while (1)
    {
        int data_read = esp_http_client_read(client, ota_buf, OTA_BUF_SIZE);
        if (data_read < 0)
        {
            ESP_LOGE(TAG, "SSL read error, errno=%d", errno);
            ota_http_cleanup(client);
            esp_ota_abort(update_handle);
            ota_set_state(OTA_UI_FAILED);
            goto done;
        }
        if (data_read > 0)
        {
            if (!image_header_checked)
            {
                esp_app_desc_t new_app_info;
                if (data_read > (int)(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) +
                                      sizeof(esp_app_desc_t)))
                {
                    memcpy(&new_app_info,
                           &ota_buf[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)],
                           sizeof(esp_app_desc_t));
                    ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

                    esp_app_desc_t running_app_info;
                    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK)
                    {
                        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
                    }

                    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                    if (err != ESP_OK)
                    {
                        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
                        ota_http_cleanup(client);
                        ota_set_state(OTA_UI_FAILED);
                        goto done;
                    }
                    image_header_checked = true;
                }
                else
                {
                    ESP_LOGE(TAG, "First packet too small");
                    ota_http_cleanup(client);
                    ota_set_state(OTA_UI_FAILED);
                    goto done;
                }
            }

            err = esp_ota_write(update_handle, ota_buf, data_read);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
                ota_http_cleanup(client);
                esp_ota_abort(update_handle);
                ota_set_state(OTA_UI_FAILED);
                goto done;
            }
            binary_file_length += data_read;
            ota_set_progress(binary_file_length, s_ota_status.content_length);
            if ((binary_file_length % (256 * 1024)) < OTA_BUF_SIZE)
            {
                ESP_LOGI(TAG, "Downloaded %d bytes...", binary_file_length);
            }
        }
        else if (data_read == 0)
        {
            if (errno == ECONNRESET || errno == ENOTCONN)
            {
                break;
            }
            if (esp_http_client_is_complete_data_received(client))
            {
                break;
            }
        }
    }

    ESP_LOGI(TAG, "Downloaded %d bytes", binary_file_length);

    if (esp_http_client_is_complete_data_received(client) != true)
    {
        ESP_LOGE(TAG, "Incomplete download");
        ota_http_cleanup(client);
        esp_ota_abort(update_handle);
        ota_set_state(OTA_UI_FAILED);
        goto done;
    }

    ota_set_state(OTA_UI_VERIFYING);
    err = esp_ota_end(update_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        ota_http_cleanup(client);
        ota_set_state(OTA_UI_FAILED);
        goto done;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        ota_http_cleanup(client);
        ota_set_state(OTA_UI_FAILED);
        goto done;
    }

    ota_http_cleanup(client);
    ota_set_state(OTA_UI_SUCCESS);
    ESP_LOGI(TAG, "OTA success, restarting...");

    if (ui_mode)
    {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ota_shutdown_subsystems();
    esp_restart();

done:
    if (s_ota_status.state != OTA_UI_SUCCESS)
    {
        printf("OTA failed. Reboot device to restore BLE and UI.\n");
    }
    heap_caps_free(ota_buf);
    free(url);
    s_ota_running = false;
    vTaskDelete(NULL);
}

static bool ota_start_common(bool ui_mode)
{
    if (s_ota_running)
    {
        return false;
    }

    if (!wifi_is_connected())
    {
        return false;
    }

    char url[OTA_URL_MAX_LEN];
    if (ota_url_get(url, sizeof(url)) != ESP_OK)
    {
        return false;
    }

    char *url_copy = strdup(url);
    if (url_copy == NULL)
    {
        return false;
    }

    s_ota_ui_mode = ui_mode;
    s_ota_running = true;
    s_ota_last_progress_us = esp_timer_get_time();
    ota_set_state(OTA_UI_PREPARING);
    ota_set_progress(0, -1);

    ota_prepare_network();
    ota_set_state(OTA_UI_PROBING);
    if (!ota_tcp_probe(url_copy))
    {
        free(url_copy);
        s_ota_running = false;
        ota_set_state(OTA_UI_FAILED);
        return false;
    }

    if (!ui_mode)
    {
        ota_shutdown_subsystems();
    }

    if (xTaskCreate(ota_task, "ota_task", 12288, url_copy, 5, NULL) != pdPASS)
    {
        free(url_copy);
        s_ota_running = false;
        ota_set_state(OTA_UI_IDLE);
        printf("Failed to create OTA task\n");
        return false;
    }
    return true;
}

bool ota_ui_is_running(void)
{
    return s_ota_running;
}

void ota_ui_get_status(ota_ui_status_t *out)
{
    if (out == NULL)
    {
        return;
    }
    *out = s_ota_status;
}

void ota_ui_format_status(char *buf, size_t len)
{
    if (buf == NULL || len == 0)
    {
        return;
    }

    switch (s_ota_status.state)
    {
    case OTA_UI_IDLE:
        snprintf(buf, len, "OTA: GO to start");
        break;
    case OTA_UI_PREPARING:
        snprintf(buf, len, "OTA: preparing...");
        break;
    case OTA_UI_PROBING:
        snprintf(buf, len, "OTA: probing srv...");
        break;
    case OTA_UI_DOWNLOADING:
        if (s_ota_status.content_length > 0)
        {
            int pct = (s_ota_status.progress_bytes * 100) / s_ota_status.content_length;
            snprintf(buf, len, "OTA: %d%% (%dKB)", pct, s_ota_status.progress_bytes / 1024);
        }
        else
        {
            snprintf(buf, len, "OTA: %dKB...", s_ota_status.progress_bytes / 1024);
        }
        break;
    case OTA_UI_VERIFYING:
        snprintf(buf, len, "OTA: verifying...");
        break;
    case OTA_UI_SUCCESS:
        snprintf(buf, len, "OTA: done reboot");
        break;
    case OTA_UI_FAILED:
        snprintf(buf, len, "OTA: failed");
        break;
    case OTA_UI_STUCK:
        snprintf(buf, len, "OTA: stuck!");
        break;
    default:
        snprintf(buf, len, "OTA: ?");
        break;
    }
}

void ota_ui_poll(void)
{
    if (!s_ota_running || s_ota_status.state != OTA_UI_DOWNLOADING)
    {
        return;
    }

    int64_t now = esp_timer_get_time();
    if ((now - s_ota_last_progress_us) > ((int64_t)OTA_STUCK_TIMEOUT_MS * 1000))
    {
        ota_set_state(OTA_UI_STUCK);
    }
}

bool ota_ui_start(void)
{
    return ota_start_common(true);
}

bool ota_console_start(void)
{
    if (s_ota_running)
    {
        printf("OTA already running\n");
        return false;
    }

    if (!wifi_is_connected())
    {
        printf("Wi-Fi not connected. Run: wifi connect <ssid> <password>\n");
        return false;
    }

    char url[OTA_URL_MAX_LEN];
    if (ota_url_get(url, sizeof(url)) != ESP_OK)
    {
        printf("Failed to read OTA URL\n");
        return false;
    }

    printf("Starting OTA from: %s\n", url);
    if (!ota_start_common(false))
    {
        if (s_ota_status.state == OTA_UI_FAILED)
        {
            printf("OTA pre-check failed\n");
        }
        return false;
    }
    return true;
}

static int cmd_ota_start(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return ota_console_start() ? 0 : 1;
}

static int cmd_ota_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    esp_app_desc_t app_info;

    if (esp_ota_get_partition_description(running, &app_info) == ESP_OK)
    {
        printf("Running : %s v%s\n", app_info.project_name, firmware_version);
    }
    if (running)
    {
        printf("Partition: %s @ 0x%lx\n", running->label, (unsigned long)running->address);
    }
    if (next)
    {
        printf("Next OTA : %s @ 0x%lx\n", next->label, (unsigned long)next->address);
    }

    char url[OTA_URL_MAX_LEN];
    if (ota_url_get(url, sizeof(url)) == ESP_OK)
    {
        printf("OTA URL  : %s\n", url);
    }
    else
    {
        printf("OTA URL  : (unavailable)\n");
    }
    return 0;
}

static int cmd_ota_url_handler(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage:\n");
        printf("  ota_url get\n");
        printf("  ota_url set <ip_or_host>\n");
        printf("  ota_url clear\n");
        return 1;
    }

    if (strcmp(argv[1], "get") == 0)
    {
        char url[OTA_URL_MAX_LEN];
        if (ota_url_get(url, sizeof(url)) != ESP_OK)
        {
            printf("Failed to read OTA URL\n");
            return 1;
        }
        printf("OTA URL: %s\n", url);
        return 0;
    }

    if (strcmp(argv[1], "set") == 0)
    {
        if (argc < 3)
        {
            printf("Usage: ota_url set <ip_or_host>\n");
            return 1;
        }

        if (ota_url_set_host(argv[2]) != ESP_OK)
        {
            printf("Failed to save OTA URL\n");
            return 1;
        }

        char url[OTA_URL_MAX_LEN];
        ota_url_get(url, sizeof(url));
        printf("OTA URL saved: %s\n", url);
        return 0;
    }

    if (strcmp(argv[1], "clear") == 0)
    {
        if (ota_url_clear() != ESP_OK)
        {
            printf("Failed to reset OTA URL in NVS\n");
            return 1;
        }

        char url[OTA_URL_MAX_LEN];
        ota_url_get(url, sizeof(url));
        printf("OTA URL reset in NVS: %s\n", url);
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
