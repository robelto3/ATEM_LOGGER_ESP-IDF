#include "ssd1306.h"

#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "SSD1306";

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;

static uint8_t s_buffer[SSD1306_BUF_SIZE];
static uint8_t s_cursor_col = 0;
static uint8_t s_cursor_page = 0;


static esp_err_t ssd1306_write_cmd(uint8_t cmd)
{
    uint8_t data[2] = {0x00, cmd};   // 0x00 = command
    return i2c_master_transmit(s_dev, data, sizeof(data), 1000);
}

static esp_err_t ssd1306_write_data(const uint8_t *data, size_t len)
{
    uint8_t temp[17];
    size_t pos = 0;

    while (pos < len) {
        size_t chunk = len - pos;
        if (chunk > 16) {
            chunk = 16;
        }

        temp[0] = 0x40;              // 0x40 = display data
        memcpy(&temp[1], &data[pos], chunk);

        esp_err_t ret = i2c_master_transmit(s_dev, temp, chunk + 1, 1000);
        if (ret != ESP_OK) {
            return ret;
        }

        pos += chunk;
    }

    return ESP_OK;
}

static esp_err_t ssd1306_set_window_start(void)
{
    ESP_RETURN_ON_ERROR(ssd1306_write_cmd(0x21), TAG, "column address cmd failed");
    ESP_RETURN_ON_ERROR(ssd1306_write_cmd(0), TAG, "column start failed");
    ESP_RETURN_ON_ERROR(ssd1306_write_cmd(SSD1306_WIDTH - 1), TAG, "column end failed");

    ESP_RETURN_ON_ERROR(ssd1306_write_cmd(0x22), TAG, "page address cmd failed");
    ESP_RETURN_ON_ERROR(ssd1306_write_cmd(0), TAG, "page start failed");
    ESP_RETURN_ON_ERROR(ssd1306_write_cmd((SSD1306_HEIGHT / 8) - 1), TAG, "page end failed");

    return ESP_OK;
}

esp_err_t ssd1306_init_with_bus(
    i2c_master_bus_handle_t bus,
    uint8_t i2c_addr,
    uint32_t clk_speed_hz
)
{
    if (!bus) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_dev != NULL) {
        return ESP_OK;
    }

    s_bus = bus;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = clk_speed_hz,
    };

    ESP_RETURN_ON_ERROR(
        i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev),
        TAG,
        "i2c add device failed"
    );

    const uint8_t init_cmds[] = {
        0xAE,
        0xD5, 0x80,
        0xA8, 0x3F,
        0xD3, 0x00,
        0x40,
        0x8D, 0x14,
        0x20, 0x00,
        0xA1,
        0xC8,
        0xDA, 0x12,
        0x81, 0x7F,
        0xD9, 0xF1,
        0xDB, 0x40,
        0xA4,
        0xA6,
        0xAF
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        ESP_RETURN_ON_ERROR(ssd1306_write_cmd(init_cmds[i]), TAG, "init command failed");
    }

    ESP_RETURN_ON_ERROR(ssd1306_clear(), TAG, "clear failed");
    ESP_RETURN_ON_ERROR(ssd1306_show(), TAG, "show failed");

    ESP_LOGI(TAG, "SSD1306 initialized on external I2C bus");

    return ESP_OK;
}

esp_err_t ssd1306_init(const ssd1306_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = cfg->i2c_port,
        .sda_io_num = cfg->sda_gpio,
        .scl_io_num = cfg->scl_gpio,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "i2c_new_master_bus failed");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = cfg->i2c_addr,
        .scl_speed_hz = cfg->clk_speed_hz,
    };

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev), TAG, "i2c add device failed");

    // Inicializační sekvence pro SSD1306 128x64
    const uint8_t init_cmds[] = {
        0xAE,       // display off
        0xD5, 0x80, // clock
        0xA8, 0x3F, // multiplex 1/64
        0xD3, 0x00, // display offset
        0x40,       // start line
        0x8D, 0x14, // charge pump on
        0x20, 0x00, // horizontal addressing mode
        0xA1,       // segment remap
        0xC8,       // COM scan direction
        0xDA, 0x12, // COM pins
        0x81, 0x7F, // contrast
        0xD9, 0xF1, // pre-charge
        0xDB, 0x40, // VCOMH
        0xA4,       // display RAM content
        0xA6,       // normal display
        0xAF        // display on
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        ESP_RETURN_ON_ERROR(ssd1306_write_cmd(init_cmds[i]), TAG, "init command failed");
    }

    ESP_RETURN_ON_ERROR(ssd1306_clear(), TAG, "clear failed");
    ESP_RETURN_ON_ERROR(ssd1306_show(), TAG, "show failed");

    ESP_LOGI(TAG, "SSD1306 initialized");
    return ESP_OK;
}

esp_err_t ssd1306_clear(void)
{
    memset(s_buffer, 0x00, sizeof(s_buffer));
    s_cursor_col = 0;
    s_cursor_page = 0;
    return ESP_OK;
}

esp_err_t ssd1306_show(void)
{
    ESP_RETURN_ON_ERROR(ssd1306_set_window_start(), TAG, "set window failed");
    return ssd1306_write_data(s_buffer, sizeof(s_buffer));
}

void ssd1306_draw_pixel(int x, int y, bool color)
{
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) {
        return;
    }

    uint16_t index = x + (y / 8) * SSD1306_WIDTH;
    uint8_t bit = 1 << (y % 8);

    if (color) {
        s_buffer[index] |= bit;
    } else {
        s_buffer[index] &= ~bit;
    }
}

void ssd1306_set_cursor(uint8_t col, uint8_t page)
{
    s_cursor_col = col;
    s_cursor_page = page;
}

// Jednoduchý 5x7 font: mezera, čísla, A-Z, pár znaků.
// Každý znak má 5 sloupců.
static const uint8_t font_space[5] = {0, 0, 0, 0, 0};

static const uint8_t font_digits[10][5] = {
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}  // 9
};

static const uint8_t font_letters[26][5] = {
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x7F,0x20,0x18,0x20,0x7F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}  // Z
};

i2c_master_bus_handle_t ssd1306_get_i2c_bus(void)
{
    return s_bus;
}

static const uint8_t *get_char_bitmap(char c)
{
    c = (char)toupper((unsigned char)c);

    if (c == ' ') return font_space;
    if (c >= '0' && c <= '9') return font_digits[c - '0'];
    if (c >= 'A' && c <= 'Z') return font_letters[c - 'A'];

    static const uint8_t colon[5] = {0x00,0x36,0x36,0x00,0x00};
    static const uint8_t dash[5]  = {0x08,0x08,0x08,0x08,0x08};
    static const uint8_t dot[5]   = {0x00,0x60,0x60,0x00,0x00};

    if (c == ':') return colon;
    if (c == '-') return dash;
    if (c == '.') return dot;

    return font_space;
}

static void draw_char(char c)
{
    const uint8_t *bitmap = get_char_bitmap(c);

    if (s_cursor_col + 6 >= SSD1306_WIDTH) {
        s_cursor_col = 0;
        s_cursor_page++;
    }

    if (s_cursor_page >= (SSD1306_HEIGHT / 8)) {
        return;
    }

    uint16_t base = s_cursor_page * SSD1306_WIDTH + s_cursor_col;

    for (int i = 0; i < 5; i++) {
        s_buffer[base + i] = bitmap[i];
    }

    s_buffer[base + 5] = 0x00; // mezera mezi znaky
    s_cursor_col += 6;
}

void ssd1306_print(const char *text)
{
    if (!text) {
        return;
    }

    while (*text) {
        if (*text == '\n') {
            s_cursor_col = 0;
            s_cursor_page++;
        } else {
            draw_char(*text);
        }

        text++;
    }
}

static void draw_char_scaled_at(int x, int y, char c, uint8_t scale)
{
    if (scale == 0) {
        scale = 1;
    }

    const uint8_t *bitmap = get_char_bitmap(c);

    for (int col = 0; col < 5; col++) {
        uint8_t bits = bitmap[col];

        for (int row = 0; row < 7; row++) {
            bool on = (bits & (1U << row)) != 0;
            if (!on) {
                continue;
            }

            for (uint8_t sx = 0; sx < scale; sx++) {
                for (uint8_t sy = 0; sy < scale; sy++) {
                    ssd1306_draw_pixel(
                        x + col * scale + sx,
                        y + row * scale + sy,
                        true
                    );
                }
            }
        }
    }
}

void ssd1306_print_scaled(int x, int y, const char *text, uint8_t scale)
{
    if (!text) {
        return;
    }

    if (scale == 0) {
        scale = 1;
    }

    // Šířka znaku je 5 sloupců fontu * scale.
    // Mezera mezi znaky je jen 1 px, aby se celý TC ve scale=2 vešel na 128 px.
    const int advance = 5 * scale + 1;
    int cursor_x = x;
    int cursor_y = y;

    while (*text) {
        if (*text == '\n') {
            cursor_x = x;
            cursor_y += 8 * scale;
        } else {
            draw_char_scaled_at(cursor_x, cursor_y, *text, scale);
            cursor_x += advance;
        }

        text++;
    }
}

