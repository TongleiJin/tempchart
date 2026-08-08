/*
 * TempChart GATT server: temperature, humidity (read), data RX (write).
 */
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "gatt_svr.h"
#include "modlog/modlog.h"
#include "user_app.h"
#include "port_rtc.h"
#include <time.h>

static const char *TAG = "gatt_svr";

static uint16_t gatt_svr_temp_handle;
static uint16_t gatt_svr_hum_handle;
static uint16_t gatt_svr_rx_handle;
static uint16_t gatt_svr_tick_handle;
static bool s_tick_notify_enabled;

static const ble_uuid16_t gatt_svr_svc_uuid = BLE_UUID16_INIT(GATT_SVR_SVC_UUID16);
static const ble_uuid16_t gatt_svr_temp_uuid = BLE_UUID16_INIT(GATT_SVR_CHR_TEMP_UUID16);
static const ble_uuid16_t gatt_svr_hum_uuid = BLE_UUID16_INIT(GATT_SVR_CHR_HUM_UUID16);
static const ble_uuid16_t gatt_svr_rx_uuid = BLE_UUID16_INIT(GATT_SVR_CHR_RX_UUID16);
static const ble_uuid16_t gatt_svr_tick_uuid = BLE_UUID16_INIT(GATT_SVR_CHR_TICK_UUID16);

static int gatt_svc_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &gatt_svr_temp_uuid.u,
                .access_cb = gatt_svc_access,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &gatt_svr_temp_handle,
            },
            {
                .uuid = &gatt_svr_hum_uuid.u,
                .access_cb = gatt_svc_access,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &gatt_svr_hum_handle,
            },
            {
                .uuid = &gatt_svr_rx_uuid.u,
                .access_cb = gatt_svc_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &gatt_svr_rx_handle,
            },
            {
                .uuid = &gatt_svr_tick_uuid.u,
                .access_cb = gatt_svc_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &gatt_svr_tick_handle,
            },
            { 0 },
        },
    },
    { 0 },
};

static void gatt_svr_log_rx_hex(const uint8_t *data, uint16_t len)
{
    if (len == 0) {
        return;
    }

    if (len <= 32) {
        char hex[32 * 3 + 1];
        size_t pos = 0;

        for (uint16_t i = 0; i < len && pos + 3 < sizeof(hex); i++) {
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02x ", data[i]);
        }
        if (pos > 0) {
            hex[pos - 1] = '\0';
        } else {
            hex[0] = '\0';
        }
        ESP_LOGI(TAG, "RX (%u bytes): %s", len, hex);
        return;
    }

    ESP_LOGI(TAG, "RX %u bytes:", len);
    for (uint16_t off = 0; off < len; off += 16) {
        char hex[16 * 3 + 1];
        uint16_t chunk = len - off;

        if (chunk > 16) {
            chunk = 16;
        }

        size_t pos = 0;
        for (uint16_t i = 0; i < chunk; i++) {
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02x ", data[off + i]);
        }
        if (pos > 0) {
            hex[pos - 1] = '\0';
        } else {
            hex[0] = '\0';
        }
        ESP_LOGI(TAG, "  %04x: %s", off, hex);
    }
}

static int gatt_svr_read_float(uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                                 float *value_out)
{
    float temperature = 0.0f;
    float humidity = 0.0f;
    float value;
    int rc;

    if (!UserApp_ReadTempHumidity(&temperature, &humidity)) {
        ESP_LOGW(TAG, "sensor read failed");
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (attr_handle == gatt_svr_temp_handle) {
        value = temperature;
    } else if (attr_handle == gatt_svr_hum_handle) {
        value = humidity;
    } else {
        return BLE_ATT_ERR_UNLIKELY;
    }

    rc = os_mbuf_append(ctxt->om, &value, sizeof(value));
    if (rc != 0) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (value_out != NULL) {
        *value_out = value;
    }
    return 0;
}

static int gatt_svr_format_tick_msg(char *buf, size_t buf_len)
{
    char time_str[16];
    TickType_t tick = xTaskGetTickCount();

    UserApp_GetTimeStr(time_str, sizeof(time_str));
    return snprintf(buf, buf_len, "tick=%lu,time=%s,kimjin",
                    (unsigned long)tick, time_str);
}

static int gatt_svr_read_tick(struct ble_gatt_access_ctxt *ctxt)
{
    char buf[GATT_SVR_TICK_MSG_MAX_LEN];
    int len = gatt_svr_format_tick_msg(buf, sizeof(buf));
    int rc;

    if (len <= 0 || len >= (int)sizeof(buf)) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    rc = os_mbuf_append(ctxt->om, buf, len);
    if (rc != 0) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    ESP_LOGD(TAG, "READ tick: %s", buf);
    return 0;
}

uint16_t gatt_svr_tick_chr_val_handle(void)
{
    return gatt_svr_tick_handle;
}

void gatt_svr_tick_notify_set(bool enabled)
{
    s_tick_notify_enabled = enabled;
}

bool gatt_svr_tick_notify_enabled(void)
{
    return s_tick_notify_enabled;
}

int gatt_svr_notify_tick(uint16_t conn_handle)
{
    char buf[GATT_SVR_TICK_MSG_MAX_LEN];
    int len = gatt_svr_format_tick_msg(buf, sizeof(buf));
    struct os_mbuf *om;
    int rc;

    if (len <= 0 || len >= (int)sizeof(buf)) {
        return BLE_HS_EINVAL;
    }

    om = ble_hs_mbuf_from_flat(buf, len);
    if (om == NULL) {
        return BLE_HS_ENOMEM;
    }

    rc = ble_gatts_notify_custom(conn_handle, gatt_svr_tick_handle, om);
    if (rc != 0) {
        os_mbuf_free_chain(om);
        ESP_LOGW(TAG, "tick notify failed: %d", rc);
        return rc;
    }

    ESP_LOGI(TAG, "NOTIFY tick: %s", buf);
    return 0;
}



// ble command string like this: sy=2026:sm=08:sd=08:, or like this: sh=18:sm=06:ss=30:
uint8_t pick_first_cmd(const char *cmdstr, char *key, uint16_t *val)
{
    char tmpbuf[64];
    if (!cmdstr || !key || !val) {
        return 0;
    }

    strcpy(tmpbuf, cmdstr);

    char *value_start = strchr(tmpbuf, '=');
    if (!value_start) {
        return 0;
    }
    *value_start = '\0'; // terminate key
    value_start++; // move to value

    char *value_end = strchr(value_start, ':');
    if (!value_end) {
        return 0;
    }
    *value_end = '\0'; // terminate value
    
    strcpy(key, tmpbuf);
    *val = (uint16_t)atoi(value_start);

    return 1;
}



// parse commands from ble cmdstr, which is text
// ble command string like this: sy=2026:sm=08:sd=08:, or like this: sh=18:sm=06:ss=30:
void do_ble_cmd(const char *cmdstr, uint16_t len){
    const char *p = cmdstr;
    char key[16];
    uint16_t val;
    struct tm rtc_time = {};

    PortRtc_GetLocalTime(&rtc_time);
    gatt_svr_log_rx_hex((const uint8_t *)cmdstr, len);

    while (pick_first_cmd(p, key, &val)) {
        ESP_LOGI(TAG, "BLE CMD: %s=%d", key, val);

        if (strcmp(key, "sy") == 0) {
            rtc_time.tm_year = val - 1900;
        } else if (strcmp(key, "sm") == 0) {
            rtc_time.tm_mon = val - 1;
        } else if (strcmp(key, "sd") == 0) {
            rtc_time.tm_mday = val;
        } else if (strcmp(key, "sh") == 0) {
            rtc_time.tm_hour = val;
        } else if (strcmp(key, "sm") == 0) {
            rtc_time.tm_min = val;
        } else if (strcmp(key, "ss") == 0) {
            rtc_time.tm_sec = val;
        }
        // move to next command
        p = strchr(p, ':');
        if (!p) {
            break;
        }
        p++; // skip ':'
    }
    // do set rtc time
    PortRtc_SetLocalTime(&rtc_time);
}


static int gatt_svc_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    float value;
    int rc;

    (void)conn_handle;
    (void)arg;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        if (attr_handle == gatt_svr_tick_handle) {
            return gatt_svr_read_tick(ctxt);
        }
        if (attr_handle == gatt_svr_temp_handle || attr_handle == gatt_svr_hum_handle) {
            rc = gatt_svr_read_float(attr_handle, ctxt, &value);
            if (rc == 0) {
                ESP_LOGD(TAG, "READ %s = %.2f",
                         attr_handle == gatt_svr_temp_handle ? "temp" : "hum",
                         value);
            }
            return rc;
        }
        break;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        if (attr_handle == gatt_svr_rx_handle) {
            uint16_t len = OS_MBUF_PKTLEN(ctxt->om);

            if (len == 0 || len > GATT_SVR_RX_MAX_LEN) {
                ESP_LOGW(TAG, "RX invalid length: %u", len);
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }

            uint8_t buf[GATT_SVR_RX_MAX_LEN];
            rc = ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
            if (rc != 0) {
                ESP_LOGE(TAG, "RX mbuf flatten failed: %d", rc);
                return BLE_ATT_ERR_UNLIKELY;
            }
            do_ble_cmd((char *)buf, len);

            return 0;
        }
        break;

    default:
        break;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    (void)arg;
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        MODLOG_DFLT(DEBUG, "registered service %s handle=%d\n",
                    ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        MODLOG_DFLT(DEBUG, "registered chr %s val_handle=%d\n",
                    ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf), ctxt->chr.val_handle);
        break;
    default:
        break;
    }
}

int gatt_svr_init(void)
{
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    return rc;
}
