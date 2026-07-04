#pragma once

#include <stddef.h>

#include "esp_err.h"

#define OTA_URL_MAX_LEN 256

esp_err_t ota_url_get(char *buf, size_t len);
esp_err_t ota_url_set_host(const char *host);
esp_err_t ota_url_clear(void);
