#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* ESP32-S3 board: 8MB Flash + 8MB Octal PSRAM */
#define BOARD_FLASH_SIZE_MB  8
#define BOARD_PSRAM_SIZE_MB  8

/* OTA download buffer (allocated from PSRAM) */
#define BOARD_OTA_BUF_SIZE   (16 * 1024)

#endif /* BOARD_CONFIG_H */
