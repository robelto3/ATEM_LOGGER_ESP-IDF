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
}   ltc_input_time_t;

typedef struct {
    uint64_t edges_total;

    uint64_t short_count;
    uint64_t long_count;
    uint64_t invalid_count;

    uint64_t bit_count;
    uint64_t zero_count;
    uint64_t one_count;
    uint64_t bit_error_count;

    uint64_t sync_count;
    uint64_t sync_3ffd_count;
    uint64_t sync_bffc_count;

    uint64_t decoded_count;
    uint64_t decode_error_count;

    uint32_t bits_since_sync;
    uint16_t sync_shift_reg;

    uint8_t tc_hours;
    uint8_t tc_minutes;
    uint8_t tc_seconds;
    uint8_t tc_frames;
    bool tc_valid;

    uint32_t last_interval_us;
    uint32_t min_interval_us;
    uint32_t max_interval_us;
    uint32_t silence_us;

    int level;
    bool signal_seen;
    bool pending_short;
    bool format_error;
} ltc_input_stats_t;

esp_err_t ltc_input_init(void);
bool ltc_input_get_time(ltc_input_time_t *time);
void ltc_input_get_stats(ltc_input_stats_t *stats);
void ltc_input_reset_stats(void);
void ltc_input_print_debug(void);

#ifdef __cplusplus
}
#endif