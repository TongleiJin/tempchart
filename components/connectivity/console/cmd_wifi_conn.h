#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"

esp_err_t wifi_stack_init(void);
void register_cmd_wifi(void);

bool wifi_is_connected(void);
bool wifi_connect_stored(int timeout_ms);
void wifi_format_status(char *buf, size_t len);
