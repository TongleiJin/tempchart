#pragma once

#include <stddef.h>
#include <stdbool.h>

void register_cmd_ota(void);

typedef enum {
    OTA_UI_IDLE = 0,
    OTA_UI_PREPARING,
    OTA_UI_PROBING,
    OTA_UI_DOWNLOADING,
    OTA_UI_VERIFYING,
    OTA_UI_SUCCESS,
    OTA_UI_FAILED,
    OTA_UI_STUCK,
} ota_ui_state_t;

typedef struct {
    ota_ui_state_t state;
    int progress_bytes;
    int content_length;
} ota_ui_status_t;

bool ota_ui_is_running(void);
void ota_ui_get_status(ota_ui_status_t *out);
void ota_ui_format_status(char *buf, size_t len);
bool ota_ui_start(void);
void ota_ui_poll(void);

bool ota_console_start(void);
