#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT   64
#define SSD1306_BUF_SIZE (SSD1306_WIDTH * SSD1306_HEIGHT / 8)

typedef struct {
    int i2c_port;
    int sda_gpio;
    int scl_gpio;
    uint8_t i2c_addr;
    uint32_t clk_speed_hz;
} ssd1306_config_t;

esp_err_t ssd1306_init(const ssd1306_config_t *cfg);

esp_err_t ssd1306_init_with_bus(
    i2c_master_bus_handle_t bus,
    uint8_t i2c_addr,
    uint32_t clk_speed_hz
);

esp_err_t ssd1306_clear(void);
esp_err_t ssd1306_show(void);
i2c_master_bus_handle_t ssd1306_get_i2c_bus(void);

void ssd1306_draw_pixel(int x, int y, bool color);
void ssd1306_set_cursor(uint8_t col, uint8_t page);
void ssd1306_print(const char *text);

// Vykreslení textu zvětšeným 5x7 fontem.
// x/y jsou pixely, scale=1 je běžná velikost, scale=2 je dvojnásobná.
void ssd1306_print_scaled(int x, int y, const char *text, uint8_t scale);

#ifdef __cplusplus
}
#endif