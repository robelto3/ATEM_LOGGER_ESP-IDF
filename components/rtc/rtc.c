#include "rtc.h"

#include <stdbool.h>
#include <string.h>

#include "ds3231.h"
#include "i2c_bus.h"

#define RTC_I2C_ADDR DS3231_I2C_ADDR
#define RTC_I2C_SPEED_HZ 400000

static ds3231_dev_t s_rtc_dev;
static bool s_rtc_ready = false;

static void rtc_copy_from_ds3231(rtc_datetime_t *dst, const ds3231_datetime_t *src)
{
    dst->seconds = src->seconds;
    dst->minutes = src->minutes;
    dst->hours   = src->hours;
    dst->day     = src->day;
    dst->date    = src->date;
    dst->month   = src->month;
    dst->year    = src->year;
}

static void rtc_copy_to_ds3231(ds3231_datetime_t *dst, const rtc_datetime_t *src)
{
    dst->seconds = src->seconds;
    dst->minutes = src->minutes;
    dst->hours   = src->hours;
    dst->day     = src->day;
    dst->date    = src->date;
    dst->month   = src->month;
    dst->year    = src->year;
}

esp_err_t rtc_init(void)
{
    if (s_rtc_ready) {
        return ESP_OK;
    }

    memset(&s_rtc_dev, 0, sizeof(s_rtc_dev));

    esp_err_t ret = ds3231_init(
        &s_rtc_dev,
        i2c_bus_get_handle(),
        RTC_I2C_ADDR,
        RTC_I2C_SPEED_HZ
    );

    if (ret == ESP_OK) {
        s_rtc_ready = true;
    }

    return ret;
}

esp_err_t rtc_read_datetime(rtc_datetime_t *dt)
{
    if (!s_rtc_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!dt) {
        return ESP_ERR_INVALID_ARG;
    }

    ds3231_datetime_t ds_dt;
    esp_err_t ret = ds3231_read_datetime(&s_rtc_dev, &ds_dt);

    if (ret != ESP_OK) {
        return ret;
    }

    rtc_copy_from_ds3231(dt, &ds_dt);

    return ESP_OK;
}

esp_err_t rtc_set_datetime(const rtc_datetime_t *dt)
{
    if (!s_rtc_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!dt) {
        return ESP_ERR_INVALID_ARG;
    }

    ds3231_datetime_t ds_dt;
    rtc_copy_to_ds3231(&ds_dt, dt);

    return ds3231_set_datetime(&s_rtc_dev, &ds_dt);
}