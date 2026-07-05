#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_URL_MAX_LEN 256

esp_err_t ota_url_get(char *buf, size_t len);
esp_err_t ota_url_set_host(const char *host);
esp_err_t ota_url_clear(void);
bool ota_url_parse_host_port(const char *url, char *host, size_t host_len, int *port);

#ifdef __cplusplus
}
#endif
