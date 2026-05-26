#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// SD karta na ESP32-P4-ETH.
// Používá onboard microSD slot přes SDMMC 4bit.
// Hlavní funkce je mount /sdcard; testovací zápis/čtení zůstává
// jen jako servisní funkce a v main se automaticky nevolá.

#define SD_STORAGE_MOUNT_POINT "/sdcard"
#define SD_STORAGE_TEST_FILE   SD_STORAGE_MOUNT_POINT "/test.txt"

esp_err_t sd_storage_init(void);
void sd_storage_deinit(void);

bool sd_storage_is_mounted(void);

esp_err_t sd_storage_write_test_file(void);
esp_err_t sd_storage_read_test_file(void);

#ifdef __cplusplus
}
#endif
