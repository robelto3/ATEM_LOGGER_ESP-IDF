#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "rtc.h"
#include "ltc.h"

#ifdef __cplusplus
extern "C" {
#endif

// Centrální EDL writer.
// Zapisuje současně do monitoru a, pokud je SD karta připojená,
// také do skutečného /sdcard/*.edl souboru.
//
// SD soubor se připraví při edl_writer_write_header().
// Každý zápis se otevře v append režimu, zapíše, flushne, fsyncne a zavře,
// aby byl obsah hned viditelný i přes web.

esp_err_t edl_writer_init(void);

void edl_writer_write_header(const char *filename,
                             const char *title,
                             const rtc_datetime_t *created_rtc,
                             bool rtc_valid);

void edl_writer_write_cut_debug(uint32_t cut_number,
                                uint8_t old_program_input,
                                uint8_t new_program_input,
                                const ltc_time_t *tc,
                                bool tc_valid);

void edl_writer_write_event(uint32_t event_number,
                            uint8_t camera,
                            const ltc_time_t *in_tc,
                            const ltc_time_t *out_tc);

void edl_writer_write_segment_start(uint8_t camera,
                                    const ltc_time_t *in_tc);

void edl_writer_write_invalid_tc_notice(void);

bool edl_writer_is_file_open(void);
const char *edl_writer_get_file_path(void);

#ifdef __cplusplus
}
#endif
