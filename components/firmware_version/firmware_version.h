#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Firmware version string, e.g. "1.0.6". Defined in firmware_version.c only. */
extern const char firmware_version[];

/** Build time as "MM-DD HH" from this firmware build. */
const char *firmware_build_month_day_hour(void);

#ifdef __cplusplus
}
#endif
