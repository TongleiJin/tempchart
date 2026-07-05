#pragma once

#include <stddef.h>

#include "esp_err.h"

void register_cmd_ntp(void);

esp_err_t ntp_sync_rtc(void);
void ntp_format_rtc_time(char *buf, size_t len);
