#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DS3231_I2C_ADDR 0x68

typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;     // 1-7, nepoužíváme nutně
    uint8_t date;    // 1-31
    uint8_t month;   // 1-12
    uint16_t year;   // např. 2026
} ds3231_datetime_t;

typedef struct {
    i2c_master_dev_handle_t dev;
} ds3231_dev_t;

esp_err_t ds3231_init(
    ds3231_dev_t *rtc,
    i2c_master_bus_handle_t bus,
    uint8_t i2c_addr,
    uint32_t clk_speed_hz
);

esp_err_t ds3231_read_datetime(ds3231_dev_t *rtc, ds3231_datetime_t *dt);
esp_err_t ds3231_set_datetime(ds3231_dev_t *rtc, const ds3231_datetime_t *dt);

#ifdef __cplusplus
}
#endif