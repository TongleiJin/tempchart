#include "ota_url_store.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "ota_url";
static const char *NVS_NAMESPACE = "ota_cfg";
static const char *NVS_KEY_URL = "url";

static esp_err_t nvs_open_ota(nvs_handle_t *handle, nvs_open_mode_t mode)
{
    return nvs_open(NVS_NAMESPACE, mode, handle);
}

static esp_err_t build_default_url(char *buf, size_t len)
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

esp_err_t ota_url_set(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open_ota(&handle, NVS_READWRITE);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_URL, url);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA URL saved to NVS: %s", url);
    }
    return err;
}

esp_err_t ota_url_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t handle;
    err = nvs_open_ota(&handle, NVS_READONLY);
    if (err != ESP_OK) {
        return err;
    }

    size_t required = 0;
    err = nvs_get_str(handle, NVS_KEY_URL, NULL, &required);
    nvs_close(handle);

    if (err == ESP_OK) {
        return ESP_OK;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    char url[OTA_URL_MAX_LEN];
    err = build_default_url(url, sizeof(url));
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Seeding default OTA URL to NVS: %s", url);
    return ota_url_set(url);
}

esp_err_t ota_url_get(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open_ota(&handle, NVS_READONLY);
    if (err != ESP_OK) {
        return err;
    }

    size_t required = len;
    err = nvs_get_str(handle, NVS_KEY_URL, buf, &required);
    nvs_close(handle);
    return err;
}

esp_err_t ota_url_set_host(const char *host)
{
    char url[OTA_URL_MAX_LEN];
    esp_err_t err = build_url_from_host(host, url, sizeof(url));
    if (err != ESP_OK) {
        return err;
    }
    return ota_url_set(url);
}

esp_err_t ota_url_clear(void)
{
    char url[OTA_URL_MAX_LEN];
    esp_err_t err = build_default_url(url, sizeof(url));
    if (err != ESP_OK) {
        return err;
    }
    return ota_url_set(url);
}
