#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

bool PortRtc_IsReady(void);
esp_err_t PortRtc_GetLocalTime(struct tm *out_time);
esp_err_t PortRtc_SetLocalTime(const struct tm *time);

#ifdef __cplusplus
}
#endif
