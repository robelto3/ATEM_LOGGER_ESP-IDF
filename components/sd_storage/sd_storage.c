#include "sd_storage.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"
#include "debug_control.h"

// ESP32-P4-ETH onboard SDMMC slot podle Waveshare schématu.
#define SD_STORAGE_PIN_CLK 43
#define SD_STORAGE_PIN_CMD 44
#define SD_STORAGE_PIN_D0  39
#define SD_STORAGE_PIN_D1  40
#define SD_STORAGE_PIN_D2  41
#define SD_STORAGE_PIN_D3  42

// ESP32-P4-ETH má napájení microSD karty spínané přes Q1.
// GPIO45 = LOW  -> karta napájená
// GPIO45 = HIGH -> karta vypnutá
#define SD_STORAGE_PIN_POWER_EN 45
#define SD_STORAGE_POWER_ON_LEVEL 0

// Pull-up odpory SD linek jsou na ESP_LDO_VO4.
#define SD_STORAGE_LDO_CHAN_ID 4

#define SD_STORAGE_MAX_FILES       10
#define SD_STORAGE_ALLOC_UNIT_SIZE (16 * 1024)

static sdmmc_card_t *s_card = NULL;
static sd_pwr_ctrl_handle_t s_pwr_ctrl_handle = NULL;
static bool s_mounted = false;

static esp_err_t sd_storage_enable_card_power(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SD_STORAGE_PIN_POWER_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        if (debug_control_is_enabled()) {
            printf("SD: GPIO45 power config FAILED: %s\n", esp_err_to_name(ret));
        }
        return ret;
    }

    ret = gpio_set_level(SD_STORAGE_PIN_POWER_EN, SD_STORAGE_POWER_ON_LEVEL);
    if (ret != ESP_OK) {
        if (debug_control_is_enabled()) {
            printf("SD: GPIO45 power enable FAILED: %s\n", esp_err_to_name(ret));
        }
        return ret;
    }

    if (debug_control_is_enabled()) {
        printf("SD: card power enabled GPIO45=LOW\n");
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    return ESP_OK;
}

static esp_err_t sd_storage_init_ldo(void)
{
    if (s_pwr_ctrl_handle != NULL) {
        return ESP_OK;
    }

    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = SD_STORAGE_LDO_CHAN_ID,
    };

    esp_err_t ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &s_pwr_ctrl_handle);
    if (ret != ESP_OK) {
        s_pwr_ctrl_handle = NULL;
        if (debug_control_is_enabled()) {
            printf("SD: LDO VO4 power-control FAILED: %s\n", esp_err_to_name(ret));
        }
        return ret;
    }

    if (debug_control_is_enabled()) {
        printf("SD: LDO VO4 power-control ready, channel=%d\n", SD_STORAGE_LDO_CHAN_ID);
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    return ESP_OK;
}

static void sd_storage_delete_ldo_if_unused(void)
{
    if (s_pwr_ctrl_handle == NULL) {
        return;
    }

    esp_err_t ret = sd_pwr_ctrl_del_on_chip_ldo(s_pwr_ctrl_handle);
    if (ret != ESP_OK) {
        if (debug_control_is_enabled()) {
            printf("SD: LDO VO4 delete FAILED: %s\n", esp_err_to_name(ret));
        }
        return;
    }

    s_pwr_ctrl_handle = NULL;
    if (debug_control_is_enabled()) {
        printf("SD: LDO VO4 power-control deleted\n");
    }
}

esp_err_t sd_storage_init(void)
{
    if (s_mounted) {
        if (debug_control_is_enabled()) {
            printf("SD: already mounted at %s\n", SD_STORAGE_MOUNT_POINT);
        }
        return ESP_OK;
    }

    if (debug_control_is_enabled()) {
        printf("SD: init ESP32-P4-ETH onboard SDMMC slot\n");
    }
    if (debug_control_is_enabled()) {
        printf(
            "SD: pins CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d width=4\n",
            SD_STORAGE_PIN_CLK,
            SD_STORAGE_PIN_CMD,
            SD_STORAGE_PIN_D0,
            SD_STORAGE_PIN_D1,
            SD_STORAGE_PIN_D2,
            SD_STORAGE_PIN_D3
        );
    }
    if (debug_control_is_enabled()) {
        printf("SD: power GPIO=%d active LOW, LDO VO4 channel=%d\n", SD_STORAGE_PIN_POWER_EN, SD_STORAGE_LDO_CHAN_ID);
    }

    esp_err_t ret = sd_storage_enable_card_power();
    if (ret != ESP_OK) {
        if (debug_control_is_enabled()) {
            printf("SD: logger continues without SD card\n");
        }
        return ret;
    }

    ret = sd_storage_init_ldo();
    if (ret != ESP_OK) {
        if (debug_control_is_enabled()) {
            printf("SD: logger continues without SD card\n");
        }
        return ret;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,   // Bezpečně: kartu zatím nikdy neformátujeme.
        .max_files = SD_STORAGE_MAX_FILES,
        .allocation_unit_size = SD_STORAGE_ALLOC_UNIT_SIZE,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;   // 20 MHz pro první bezpečný test
    host.pwr_ctrl_handle = s_pwr_ctrl_handle;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.clk = SD_STORAGE_PIN_CLK;
    slot_config.cmd = SD_STORAGE_PIN_CMD;
    slot_config.d0 = SD_STORAGE_PIN_D0;
    slot_config.d1 = SD_STORAGE_PIN_D1;
    slot_config.d2 = SD_STORAGE_PIN_D2;
    slot_config.d3 = SD_STORAGE_PIN_D3;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ret = esp_vfs_fat_sdmmc_mount(
        SD_STORAGE_MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &s_card
    );

    if (ret != ESP_OK) {
        if (debug_control_is_enabled()) {
            printf("SD: mount FAILED: %s\n", esp_err_to_name(ret));
        }
        if (debug_control_is_enabled()) {
            printf("SD: logger continues without SD card\n");
        }
        s_card = NULL;
        s_mounted = false;
        sd_storage_delete_ldo_if_unused();
        return ret;
    }

    s_mounted = true;

    if (debug_control_is_enabled()) {
        printf("SD: mounted at %s\n", SD_STORAGE_MOUNT_POINT);
    }
    if (debug_control_is_enabled()) {
        sdmmc_card_print_info(stdout, s_card);
    }

    return ESP_OK;
}

void sd_storage_deinit(void)
{
    if (s_mounted && s_card != NULL) {
        esp_vfs_fat_sdcard_unmount(SD_STORAGE_MOUNT_POINT, s_card);
        s_card = NULL;
        s_mounted = false;
        if (debug_control_is_enabled()) {
            printf("SD: unmounted\n");
        }
    }

    sd_storage_delete_ldo_if_unused();
}

bool sd_storage_is_mounted(void)
{
    return s_mounted;
}

esp_err_t sd_storage_get_space(uint64_t *total_bytes, uint64_t *free_bytes)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_vfs_fat_info(SD_STORAGE_MOUNT_POINT, total_bytes, free_bytes);
}

esp_err_t sd_storage_write_test_file(void)
{
    if (!s_mounted) {
        if (debug_control_is_enabled()) {
            printf("SD: write test skipped, card is not mounted\n");
        }
        return ESP_ERR_INVALID_STATE;
    }

    if (debug_control_is_enabled()) {
        printf("SD: writing %s\n", SD_STORAGE_TEST_FILE);
    }

    FILE *file = fopen(SD_STORAGE_TEST_FILE, "w");
    if (file == NULL) {
        if (debug_control_is_enabled()) {
            printf("SD: fopen write FAILED\n");
        }
        return ESP_FAIL;
    }

    fprintf(file, "ATEM Logger SD test OK\n");
    fprintf(file, "mount=%s\n", SD_STORAGE_MOUNT_POINT);
    fclose(file);

    if (debug_control_is_enabled()) {
        printf("SD: write test OK\n");
    }
    return ESP_OK;
}

esp_err_t sd_storage_read_test_file(void)
{
    if (!s_mounted) {
        if (debug_control_is_enabled()) {
            printf("SD: read test skipped, card is not mounted\n");
        }
        return ESP_ERR_INVALID_STATE;
    }

    if (debug_control_is_enabled()) {
        printf("SD: reading %s\n", SD_STORAGE_TEST_FILE);
    }

    FILE *file = fopen(SD_STORAGE_TEST_FILE, "r");
    if (file == NULL) {
        if (debug_control_is_enabled()) {
            printf("SD: fopen read FAILED\n");
        }
        return ESP_FAIL;
    }

    char line[128] = {0};
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (debug_control_is_enabled()) {
            printf("SD: read: %s\n", line);
        }
    }

    fclose(file);

    if (debug_control_is_enabled()) {
        printf("SD: read test OK\n");
    }
    return ESP_OK;
}
