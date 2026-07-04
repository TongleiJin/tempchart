#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool connected;
    char ssid[33];
    int8_t rssi;
    char ip[16];
    char gateway[16];
} wifi_sta_status_t;

bool wifi_sta_get_status(wifi_sta_status_t *status);

#ifdef __cplusplus
}
#endif
