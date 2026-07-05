#include "firmware_version.h"

#include <stdbool.h>
#include <stdio.h>

#include "esp_app_desc.h"
#include "sdkconfig.h"

/*
 * Single source of truth for firmware version.
 * Change FIRMWARE_VERSION_STRING here; only this file is recompiled.
 */
#define FIRMWARE_VERSION_STRING "0.1.4"
#define FIRMWARE_PROJECT_NAME "smart_tempchart"

const char firmware_version[] = FIRMWARE_VERSION_STRING;

static int parse_month_from_date(const char *date)
{
    static const char *names[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };

    for (int i = 0; i < 12; ++i) {
        if (date[0] == names[i][0] && date[1] == names[i][1] && date[2] == names[i][2]) {
            return i + 1;
        }
    }
    return 0;
}

const char *firmware_build_month_day_hour(void)
{
    static char s_buf[14];
    static bool s_initialized;

    if (!s_initialized) {
        const char *date = __DATE__;
        const char *time = __TIME__;
        int month = parse_month_from_date(date);
        int day = 0;
        int hour = 0;
        int minute = 0;

        sscanf(date + 4, "%d", &day);
        sscanf(time, "%d:%d", &hour, &minute);
        snprintf(s_buf, sizeof(s_buf), "%02d-%02d %02d:%02d", month, day, hour, minute);
        s_initialized = true;
    }

    return s_buf;
}

_Static_assert(sizeof(FIRMWARE_VERSION_STRING) <= sizeof(((esp_app_desc_t *)0)->version),
               "FIRMWARE_VERSION_STRING too long");
_Static_assert(sizeof(FIRMWARE_PROJECT_NAME) <= sizeof(((esp_app_desc_t *)0)->project_name),
               "FIRMWARE_PROJECT_NAME too long");
_Static_assert(sizeof(IDF_VER) <= sizeof(((esp_app_desc_t *)0)->idf_ver), "IDF_VER too long");

/* Strong symbol overrides weak esp_app_desc in esp_app_format. */
const esp_app_desc_t esp_app_desc __attribute__((section(".rodata_desc"))) = {
    .magic_word = ESP_APP_DESC_MAGIC_WORD,
#ifdef CONFIG_BOOTLOADER_APP_SECURE_VERSION
    .secure_version = CONFIG_BOOTLOADER_APP_SECURE_VERSION,
#else
    .secure_version = 0,
#endif
    .version = FIRMWARE_VERSION_STRING,
    .project_name = FIRMWARE_PROJECT_NAME,
#ifdef CONFIG_APP_COMPILE_TIME_DATE
    .time = __TIME__,
    .date = __DATE__,
#else
    .time = "",
    .date = "",
#endif
    .idf_ver = IDF_VER,
    .min_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MIN_FULL,
    .max_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MAX_FULL,
    .mmu_page_size = 31 - __builtin_clz(CONFIG_MMU_PAGE_SIZE),
};
