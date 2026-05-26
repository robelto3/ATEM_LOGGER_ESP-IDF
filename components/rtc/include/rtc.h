#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t date;
    uint8_t month;
    uint16_t year;
} rtc_datetime_t;

esp_err_t rtc_init(void);
esp_err_t rtc_read_datetime(rtc_datetime_t *dt);
esp_err_t rtc_set_datetime(const rtc_datetime_t *dt);

#ifdef __cplusplus
}
#endif