#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Fill buf with firmware / network / BLE summary for the home info page. */
void device_status_page2(char *buf, size_t size);
void device_status_page1(char *buf, size_t size);

#ifdef __cplusplus
}
#endif
