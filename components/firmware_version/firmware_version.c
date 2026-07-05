#include "firmware_version.h"

#include "esp_app_desc.h"
#include "sdkconfig.h"

/*
 * Single source of truth for firmware version.
 * Change FIRMWARE_VERSION_STRING here; only this file is recompiled.
 */
#define FIRMWARE_VERSION_STRING "1.0.6"
#define FIRMWARE_PROJECT_NAME "tempchart"

const char firmware_version[] = FIRMWARE_VERSION_STRING;

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
