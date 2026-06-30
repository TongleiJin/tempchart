#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "host/ble_gatt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* TempChart custom GATT service (16-bit UUIDs) */
#define GATT_SVR_SVC_UUID16       0xFC01
#define GATT_SVR_CHR_TEMP_UUID16  0xFC02
#define GATT_SVR_CHR_HUM_UUID16   0xFC03
#define GATT_SVR_CHR_RX_UUID16    0xFC04
#define GATT_SVR_CHR_TICK_UUID16  0xFC05

#define GATT_SVR_RX_MAX_LEN       512
#define GATT_SVR_TICK_MSG_MAX_LEN 64

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
int gatt_svr_init(void);
uint16_t gatt_svr_tick_chr_val_handle(void);
void gatt_svr_tick_notify_set(bool enabled);
bool gatt_svr_tick_notify_enabled(void);
int gatt_svr_notify_tick(uint16_t conn_handle);

#ifdef __cplusplus
}
#endif
