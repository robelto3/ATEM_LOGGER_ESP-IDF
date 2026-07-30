#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
    uint8_t frames;
    bool valid;
    bool format_error;
} ltc_time_t;

esp_err_t ltc_init(void);
bool ltc_get_time(ltc_time_t *time);
void ltc_print_debug(void);

#ifdef __cplusplus
}
#endif