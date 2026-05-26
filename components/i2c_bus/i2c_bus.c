#include "i2c_bus.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

#define I2C_BUS_PORT     I2C_NUM_0
#define I2C_BUS_SDA_GPIO GPIO_NUM_7
#define I2C_BUS_SCL_GPIO GPIO_NUM_8

static const char *TAG = "I2C_BUS";

static i2c_master_bus_handle_t s_i2c_bus = NULL;

esp_err_t i2c_bus_init(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_BUS_PORT,
        .sda_io_num = I2C_BUS_SDA_GPIO,
        .scl_io_num = I2C_BUS_SCL_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(
        i2c_new_master_bus(&bus_cfg, &s_i2c_bus),
        TAG,
        "i2c_new_master_bus failed"
    );

    ESP_LOGI(TAG, "I2C bus initialized SDA=GPIO7 SCL=GPIO8");

    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_get_handle(void)
{
    return s_i2c_bus;
}