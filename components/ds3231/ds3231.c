#include "ds3231.h"

#include <string.h>
#include "esp_check.h"

static const char *TAG = "DS3231";

static uint8_t bcd_to_dec(uint8_t val)
{
    return ((val >> 4) * 10) + (val & 0x0F);
}

static uint8_t dec_to_bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

esp_err_t ds3231_init(
    ds3231_dev_t *rtc,
    i2c_master_bus_handle_t bus,
    uint8_t i2c_addr,
    uint32_t clk_speed_hz
)
{
    if (!rtc || !bus) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(rtc, 0, sizeof(*rtc));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = clk_speed_hz,
    };

    return i2c_master_bus_add_device(bus, &dev_cfg, &rtc->dev);
}

esp_err_t ds3231_read_datetime(ds3231_dev_t *rtc, ds3231_datetime_t *dt)
{
    if (!rtc || !dt || !rtc->dev) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg = 0x00;
    uint8_t data[7] = {0};

    ESP_RETURN_ON_ERROR(
        i2c_master_transmit_receive(rtc->dev, &reg, 1, data, sizeof(data), 1000),
        TAG,
        "read datetime failed"
    );

    dt->seconds = bcd_to_dec(data[0] & 0x7F);
    dt->minutes = bcd_to_dec(data[1] & 0x7F);

    // 24hodinový režim
    dt->hours = bcd_to_dec(data[2] & 0x3F);

    dt->day = bcd_to_dec(data[3] & 0x07);
    dt->date = bcd_to_dec(data[4] & 0x3F);
    dt->month = bcd_to_dec(data[5] & 0x1F);
    dt->year = 2000 + bcd_to_dec(data[6]);

    return ESP_OK;
}

esp_err_t ds3231_set_datetime(ds3231_dev_t *rtc, const ds3231_datetime_t *dt)
{
    if (!rtc || !dt || !rtc->dev) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[8];

    data[0] = 0x00; // start register
    data[1] = dec_to_bcd(dt->seconds);
    data[2] = dec_to_bcd(dt->minutes);
    data[3] = dec_to_bcd(dt->hours);   // 24h mode
    data[4] = dec_to_bcd(dt->day);
    data[5] = dec_to_bcd(dt->date);
    data[6] = dec_to_bcd(dt->month);
    data[7] = dec_to_bcd((uint8_t)(dt->year - 2000));

    return i2c_master_transmit(rtc->dev, data, sizeof(data), 1000);
}