#pragma once

#include <stdint.h>

#ifndef APP_VERSION_CODE
#define APP_VERSION_CODE 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

uint32_t app_version_code_from_string(const char *version);
int app_version_compare_strings(const char *lhs, const char *rhs);
uint32_t app_version_running_code(void);

#ifdef __cplusplus
}
#endif
