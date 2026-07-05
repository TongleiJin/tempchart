#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SETTING_MODE_SAMPLE_INTV  0
#define SETTING_MODE_CHART_POINTS 1
#define SETTING_MODE_SENSOR_SRC   2
#define SETTING_MODE_WIFI         3
#define SETTING_MODE_NTP          4
#define SETTING_MODE_OTA          5
#define SETTING_MODE_RESTART      6
#define SETTING_MODE_EPD_REINIT   7
#define SETTING_MODE_COUNT        8

typedef struct setting_actions_ops {
    void (*run_wifi_connect)(void);
    void (*run_ntp_sync)(void);
    void (*run_ota)(void);
    void (*run_reboot)(void);
    void (*run_epd_reinit)(void);
    void (*format_mode_label)(int mode, char *buf, size_t len);
    bool (*is_busy)(void);
    void (*poll)(void);
} setting_actions_ops_t;

void setting_actions_register(const setting_actions_ops_t *ops);
const setting_actions_ops_t *setting_actions_get(void);

#ifdef __cplusplus
}
#endif
