/*
 * GATT server (from ESP-IDF nimble/bleprph example, simplified)
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/ans/ble_svc_ans.h"
#include "gatt_svr.h"
#include "modlog/modlog.h"

static uint8_t gatt_svr_chr_val;
static uint16_t gatt_svr_chr_val_handle;
static const ble_uuid128_t gatt_svr_svc_uuid =
    BLE_UUID128_INIT(0x2d, 0x71, 0xa2, 0x59, 0xb4, 0x58, 0xc8, 0x12,
                     0x99, 0x99, 0x43, 0x95, 0x12, 0x2f, 0x46, 0x59);
static const ble_uuid128_t gatt_svr_chr_uuid =
    BLE_UUID128_INIT(0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0x11, 0x11,
                     0x22, 0x22, 0x22, 0x22, 0x33, 0x33, 0x33, 0x33);

static int gatt_svc_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &gatt_svr_chr_uuid.u,
                .access_cb = gatt_svc_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE,
                .val_handle = &gatt_svr_chr_val_handle,
            },
            { 0 },
        },
    },
    { 0 },
};

static int gatt_svr_write(struct os_mbuf *om, uint16_t min_len, uint16_t max_len,
                          void *dst, uint16_t *len)
{
    uint16_t om_len = OS_MBUF_PKTLEN(om);
    if (om_len < min_len || om_len > max_len) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    return ble_hs_mbuf_to_flat(om, dst, max_len, len);
}

static int gatt_svc_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        if (attr_handle == gatt_svr_chr_val_handle) {
            rc = os_mbuf_append(ctxt->om, &gatt_svr_chr_val, sizeof(gatt_svr_chr_val));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        break;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        if (attr_handle == gatt_svr_chr_val_handle) {
            rc = gatt_svr_write(ctxt->om, sizeof(gatt_svr_chr_val),
                                sizeof(gatt_svr_chr_val), &gatt_svr_chr_val, NULL);
            ble_gatts_chr_updated(attr_handle);
            return rc;
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
    ble_svc_ans_init();

    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    return rc;
}
