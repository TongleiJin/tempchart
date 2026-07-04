/*
 * NimBLE peripheral (from ESP-IDF nimble/bleprph example, simplified)
 */
#include "ble_app.h"

#include <string.h>

#include "esp_log.h"
#include "esp_coexist.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "gatt_svr.h"

static const char *TAG = "ble_app";
static uint8_t own_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static TaskHandle_t s_tick_notify_task;

#define BLE_TICK_NOTIFY_INTERVAL_MS 1000

static void print_addr(const uint8_t *addr)
{
    ESP_LOGI(TAG, "%02x:%02x:%02x:%02x:%02x:%02x",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

static int ble_gap_event(struct ble_gap_event *event, void *arg);
static void ble_on_sync(void);
static void ble_on_reset(int reason);
static void ble_advertise(void);
static void ble_tick_notify_task(void *param);

static void ble_tick_notify_task(void *param)
{
    (void)param;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(BLE_TICK_NOTIFY_INTERVAL_MS));

        if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            continue;
        }

        if (!gatt_svr_tick_notify_enabled()) {
            continue;
        }

        gatt_svr_notify_tick(s_conn_handle);
    }
}

void ble_store_config_init(void);

static void ble_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    fields.uuids16 = (ble_uuid16_t[]) {
        BLE_UUID16_INIT(GATT_SVR_SVC_UUID16)
    };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv set fields failed: %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv start failed: %d", rc);
    }
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
        } else {
            ble_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect; reason=%d", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        gatt_svr_tick_notify_set(false);
        ble_advertise();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == gatt_svr_tick_chr_val_handle()) {
            gatt_svr_tick_notify_set(event->subscribe.cur_notify);
            ESP_LOGI(TAG, "tick notify %s",
                     event->subscribe.cur_notify ? "enabled" : "disabled");
        }
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ble_advertise();
        return 0;

    default:
        return 0;
    }
}

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset; reason=%d", reason);
}

static void ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer addr type failed: %d", rc);
        return;
    }

    uint8_t addr_val[6] = {0};
    ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
    print_addr(addr_val);

    ble_advertise();
}

static void ble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_app_init(void)
{
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        return ret;
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;

    int rc = gatt_svr_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "gatt_svr_init failed: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_svc_gap_device_name_set(CONFIG_APP_BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "set device name failed: %d", rc);
        return ESP_FAIL;
    }

    ble_store_config_init();
    nimble_port_freertos_init(ble_host_task);

    if (s_tick_notify_task == NULL) {
        xTaskCreate(ble_tick_notify_task, "ble_tick_notify", 3072, NULL, 5,
                    &s_tick_notify_task);
    }

    ESP_LOGI(TAG, "NimBLE peripheral ready, name=%s", CONFIG_APP_BLE_DEVICE_NAME);
    return ESP_OK;
}

esp_err_t ble_app_shutdown_for_ota(void)
{
    ESP_LOGI(TAG, "Stopping BLE for OTA...");

    if (s_tick_notify_task != NULL) {
        vTaskDelete(s_tick_notify_task);
        s_tick_notify_task = NULL;
    }

    int rc = nimble_port_stop();
    if (rc != 0) {
        ESP_LOGW(TAG, "nimble_port_stop returned %d", rc);
    }

    esp_err_t ret = nimble_port_deinit();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "nimble_port_deinit: %s", esp_err_to_name(ret));
    }

    esp_coex_preference_set(ESP_COEX_PREFER_WIFI);
    return ret;
}
