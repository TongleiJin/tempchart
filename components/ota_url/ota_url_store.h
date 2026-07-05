#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_URL_MAX_LEN 256

/** Init NVS OTA URL namespace; seed default URL on first boot. */
esp_err_t ota_url_store_init(void);

/** Read OTA URL from NVS only. Call ota_url_store_init() first. */
esp_err_t ota_url_get(char *buf, size_t len);

/** Save full OTA URL to NVS. */
esp_err_t ota_url_set(const char *url);

/** Build URL from host and save to NVS. */
esp_err_t ota_url_set_host(const char *host);

/** Reset OTA URL in NVS to the factory default. */
esp_err_t ota_url_clear(void);

#ifdef __cplusplus
}
#endif
