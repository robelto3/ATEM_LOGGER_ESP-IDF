#include "ltc.h"

#include "ltc_input.h"

esp_err_t ltc_init(void)
{
    return ltc_input_init();
}

bool ltc_get_time(ltc_time_t *time)
{
    if (!time) {
        return false;
    }

    ltc_input_time_t input_time;

    bool valid = ltc_input_get_time(&input_time);

    time->hours = input_time.hours;
    time->minutes = input_time.minutes;
    time->seconds = input_time.seconds;
    time->frames = input_time.frames;
    time->valid = input_time.valid;
    time->format_error = input_time.format_error;

    return valid;
}

void ltc_print_debug(void)
{
    ltc_input_print_debug();
}