#include "ota_url_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "ota_url";
static const char *NVS_NAMESPACE = "ota_cfg";
static const char *NVS_KEY_URL = "url";

static esp_err_t nvs_open_ota(nvs_handle_t *handle, nvs_open_mode_t mode)
{
    return nvs_open(NVS_NAMESPACE, mode, handle);
}

static esp_err_t copy_default_url(char *buf, size_t len)
{
    if (len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = snprintf(buf, len, "%s", CONFIG_APP_OTA_FIRMWARE_URL);
    if (written < 0 || (size_t)written >= len) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t build_url_from_host(const char *host, char *buf, size_t len)
{
    if (host == NULL || host[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    int written = snprintf(buf, len, "https://%s:%d%s",
                           host, CONFIG_APP_OTA_SERVER_PORT, CONFIG_APP_OTA_FIRMWARE_PATH);
    if (written < 0 || (size_t)written >= len) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

bool ota_url_parse_host_port(const char *url, char *host, size_t host_len, int *port)
{
    if (url == NULL || host == NULL || host_len == 0 || port == NULL) {
        return false;
    }

    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) {
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    }

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    size_t len;

    if (colon != NULL && (slash == NULL || colon < slash)) {
        len = (size_t)(colon - p);
        if (len == 0 || len >= host_len) {
            return false;
        }
        memcpy(host, p, len);
        host[len] = '\0';
        *port = atoi(colon + 1);
    } else {
        len = slash ? (size_t)(slash - p) : strlen(p);
        if (len == 0 || len >= host_len) {
            return false;
        }
        memcpy(host, p, len);
        host[len] = '\0';
        *port = 443;
    }

    return *port > 0;
}

esp_err_t ota_url_get(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open_ota(&handle, NVS_READONLY);
    if (err != ESP_OK) {
        return copy_default_url(buf, len);
    }

    size_t required = len;
    err = nvs_get_str(handle, NVS_KEY_URL, buf, &required);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return copy_default_url(buf, len);
    }
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        return ESP_ERR_INVALID_SIZE;
    }
    return err;
}

esp_err_t ota_url_set_host(const char *host)
{
    char url[OTA_URL_MAX_LEN];
    esp_err_t err = build_url_from_host(host, url, sizeof(url));
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t handle;
    err = nvs_open_ota(&handle, NVS_READWRITE);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_URL, url);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA URL saved: %s", url);
    }
    return err;
}

esp_err_t ota_url_clear(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_ota(&handle, NVS_READWRITE);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(handle, NVS_KEY_URL);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
