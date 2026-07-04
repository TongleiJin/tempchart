#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ble_app_init(void);
esp_err_t ble_app_shutdown_for_ota(void);

#ifdef __cplusplus
}
#endif
