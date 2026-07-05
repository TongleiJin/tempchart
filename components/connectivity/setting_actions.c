#include "setting_actions.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "port_display.h"
#include "port_lvgl.h"
#include "setting_actions_iface.h"
#include "cmd_wifi_conn.h"
#include "cmd_ntp.h"
#include "cmd_ota.h"

static const char *TAG = "set_act";

#define WIFI_CONNECT_TIMEOUT_MS 15000

static char s_status_msg[80];
static volatile bool s_wifi_busy;
static volatile bool s_ntp_busy;

static void set_status(const char *msg)
{
    strlcpy(s_status_msg, msg, sizeof(s_status_msg));
}

static void wifi_connect_task(void *arg)
{
    (void)arg;
    s_wifi_busy = true;
    set_status("WiFi: connecting...");

    if (wifi_connect_stored(WIFI_CONNECT_TIMEOUT_MS)) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            snprintf(s_status_msg, sizeof(s_status_msg), "WiFi: OK %ddBm", ap_info.rssi);
        } else {
            set_status("WiFi: connected");
        }
    } else {
        set_status("WiFi: failed");
    }

    s_wifi_busy = false;
    vTaskDelete(NULL);
}

static void ntp_sync_task(void *arg)
{
    (void)arg;
    s_ntp_busy = true;
    set_status("NTP: syncing...");

    esp_err_t err = ntp_sync_rtc();
    if (err == ESP_OK) {
        ntp_format_rtc_time(s_status_msg, sizeof(s_status_msg));
    } else {
        set_status("NTP: failed");
    }

    s_ntp_busy = false;
    vTaskDelete(NULL);
}

static void run_wifi_connect(void)
{
    if (s_wifi_busy || s_ntp_busy || ota_ui_is_running()) {
        return;
    }

    if (wifi_is_connected()) {
        wifi_format_status(s_status_msg, sizeof(s_status_msg));
        return;
    }

    if (xTaskCreate(wifi_connect_task, "set_wifi", 4096, NULL, 3, NULL) != pdPASS) {
        set_status("WiFi: task err");
    }
}

static void run_ntp_sync(void)
{
    if (s_ntp_busy || ota_ui_is_running()) {
        return;
    }

    if (xTaskCreate(ntp_sync_task, "set_ntp", 4096, NULL, 3, NULL) != pdPASS) {
        set_status("NTP: task err");
    }
}

static void run_ota(void)
{
    if (s_wifi_busy || s_ntp_busy || ota_ui_is_running()) {
        return;
    }

    if (!wifi_is_connected()) {
        ota_ui_format_status(s_status_msg, sizeof(s_status_msg));
        set_status("OTA: need WiFi");
        return;
    }

    if (!ota_ui_start()) {
        ota_ui_format_status(s_status_msg, sizeof(s_status_msg));
    }
}

static void reboot_task(void *arg)
{
    (void)arg;
    set_status("Restarting...");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static void run_reboot(void)
{
    if (ota_ui_is_running()) {
        return;
    }

    if (xTaskCreate(reboot_task, "set_reboot", 2048, NULL, 3, NULL) != pdPASS) {
        set_status("Restart: err");
    }
}

static void epd_reinit_task(void *arg)
{
    (void)arg;
    set_status("EPD: reinit...");
    Lvgl_PauseRefresh();
    PortDisplay_Reinit();
    Lvgl_ResumeRefresh();

    if (Lvgl_lock(5000)) {
        lv_obj_invalidate(lv_screen_active());
        Lvgl_unlock();
    }

    set_status("EPD: done");
    vTaskDelete(NULL);
}

static void run_epd_reinit(void)
{
    if (s_wifi_busy || s_ntp_busy || ota_ui_is_running()) {
        return;
    }

    if (xTaskCreate(epd_reinit_task, "set_epd", 4096, NULL, 3, NULL) != pdPASS) {
        set_status("EPD: task err");
    }
}

static void format_mode_label(int mode, char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }

    switch (mode) {
    case SETTING_MODE_WIFI:
        if (s_wifi_busy) {
            snprintf(buf, len, "%s", s_status_msg);
        } else if (s_status_msg[0] != '\0' && strncmp(s_status_msg, "WiFi:", 5) == 0) {
            snprintf(buf, len, "%s", s_status_msg);
        } else {
            wifi_format_status(buf, len);
            snprintf(buf + strlen(buf), len - strlen(buf), " GO");
        }
        break;
    case SETTING_MODE_NTP:
        if (s_ntp_busy) {
            snprintf(buf, len, "%s", s_status_msg);
        } else if (s_status_msg[0] != '\0' && strncmp(s_status_msg, "NTP:", 4) == 0) {
            snprintf(buf, len, "%s", s_status_msg);
        } else if (s_status_msg[0] != '\0' && strncmp(s_status_msg, "RTC:", 4) == 0) {
            snprintf(buf, len, "%s GO", s_status_msg);
        } else {
            ntp_format_rtc_time(buf, len);
            snprintf(buf + strlen(buf), len - strlen(buf), " GO");
        }
        break;
    case SETTING_MODE_OTA:
        ota_ui_format_status(buf, len);
        break;
    case SETTING_MODE_RESTART:
        snprintf(buf, len, "Restart: GO");
        break;
    case SETTING_MODE_EPD_REINIT:
        if (s_status_msg[0] != '\0' && strncmp(s_status_msg, "EPD:", 4) == 0) {
            snprintf(buf, len, "%s", s_status_msg);
        } else {
            snprintf(buf, len, "EPD: GO reinit");
        }
        break;
    default:
        buf[0] = '\0';
        break;
    }
}

static bool is_busy(void)
{
    return s_wifi_busy || s_ntp_busy || ota_ui_is_running();
}

static void poll(void)
{
    ota_ui_poll();
}

void setting_actions_init(void)
{
    static const setting_actions_ops_t ops = {
        .run_wifi_connect = run_wifi_connect,
        .run_ntp_sync = run_ntp_sync,
        .run_ota = run_ota,
        .run_reboot = run_reboot,
        .run_epd_reinit = run_epd_reinit,
        .format_mode_label = format_mode_label,
        .is_busy = is_busy,
        .poll = poll,
    };

    setting_actions_register(&ops);
    ESP_LOGI(TAG, "Setting actions registered");
}
