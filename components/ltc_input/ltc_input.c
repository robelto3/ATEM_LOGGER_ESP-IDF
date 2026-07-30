#include "ltc_input.h"

#include <stdio.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "debug_control.h"

#define LTC_INPUT_GPIO GPIO_NUM_4

#define LTC_SHORT_MIN_US 180
#define LTC_SHORT_MAX_US 350
#define LTC_LONG_MIN_US  400
#define LTC_LONG_MAX_US  650

#define LTC_SYNC_3FFD 0x3FFD
#define LTC_SYNC_BFFC 0xBFFC

#define LTC_FRAME_BITS 80

// Když po posledním platném LTC framu nepřijde další frame do této doby,
// považujeme LTC za neplatné. Při 25 fps přichází frame cca každých 40 ms,
// takže 500 ms je bezpečná rezerva bez zbytečného blikání stavu.
#define LTC_VALID_TIMEOUT_US 500000LL

static portMUX_TYPE s_ltc_mux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool s_ready = false;

static volatile uint64_t s_edges_total = 0;

static volatile uint64_t s_short_count = 0;
static volatile uint64_t s_long_count = 0;
static volatile uint64_t s_invalid_count = 0;

static volatile uint64_t s_bit_count = 0;
static volatile uint64_t s_zero_count = 0;
static volatile uint64_t s_one_count = 0;
static volatile uint64_t s_bit_error_count = 0;

static volatile uint64_t s_sync_count = 0;
static volatile uint64_t s_sync_3ffd_count = 0;
static volatile uint64_t s_sync_bffc_count = 0;

static volatile uint64_t s_decoded_count = 0;
static volatile uint64_t s_decode_error_count = 0;

static volatile uint32_t s_bits_since_sync = 0;
static volatile uint16_t s_sync_shift_reg = 0;

static volatile bool s_pending_short = false;

static volatile int64_t s_last_edge_time_us = 0;
static volatile uint32_t s_last_interval_us = 0;
static volatile uint32_t s_min_interval_us = UINT32_MAX;
static volatile uint32_t s_max_interval_us = 0;

static volatile uint8_t s_bit_ring[LTC_FRAME_BITS];
static volatile uint8_t s_bit_write_pos = 0;
static volatile uint8_t s_bits_collected = 0;

static volatile bool s_tc_valid = false;
static volatile uint8_t s_tc_hours = 0;
static volatile uint8_t s_tc_minutes = 0;
static volatile uint8_t s_tc_seconds = 0;
static volatile uint8_t s_tc_frames = 0;
static volatile int64_t s_last_valid_frame_time_us = 0;
static volatile bool s_format_error = false;
static volatile int64_t s_last_format_error_time_us = 0;

static void IRAM_ATTR ltc_mark_format_error_isr(void)
{
    s_format_error = true;
    s_last_format_error_time_us = s_last_edge_time_us;
}

static void IRAM_ATTR ltc_store_bit_isr(uint8_t bit)
{
    s_bit_ring[s_bit_write_pos] = bit & 0x01;
    s_bit_write_pos++;

    if (s_bit_write_pos >= LTC_FRAME_BITS) {
        s_bit_write_pos = 0;
    }

    if (s_bits_collected < LTC_FRAME_BITS) {
        s_bits_collected++;
    }
}

static uint8_t IRAM_ATTR ltc_get_frame_bit_isr(uint8_t index)
{
    uint8_t pos = s_bit_write_pos + index;

    if (pos >= LTC_FRAME_BITS) {
        pos -= LTC_FRAME_BITS;
    }

    return s_bit_ring[pos] & 0x01;
}

static uint8_t IRAM_ATTR ltc_get_bcd_units_isr(uint8_t start_bit)
{
    return (uint8_t)(
        (ltc_get_frame_bit_isr(start_bit + 0) << 0) |
        (ltc_get_frame_bit_isr(start_bit + 1) << 1) |
        (ltc_get_frame_bit_isr(start_bit + 2) << 2) |
        (ltc_get_frame_bit_isr(start_bit + 3) << 3)
    );
}

static uint8_t IRAM_ATTR ltc_get_bcd_tens_isr(uint8_t start_bit, uint8_t bit_count)
{
    uint8_t value = 0;

    for (uint8_t i = 0; i < bit_count; i++) {
        value |= (uint8_t)(ltc_get_frame_bit_isr(start_bit + i) << i);
    }

    return value;
}

static void IRAM_ATTR ltc_decode_frame_isr(void)
{
    if (s_bits_collected < LTC_FRAME_BITS) {
        return;
    }

    uint8_t frame_units = ltc_get_bcd_units_isr(0);
    uint8_t frame_tens  = ltc_get_bcd_tens_isr(8, 2);

    uint8_t sec_units = ltc_get_bcd_units_isr(16);
    uint8_t sec_tens  = ltc_get_bcd_tens_isr(24, 3);

    uint8_t min_units = ltc_get_bcd_units_isr(32);
    uint8_t min_tens  = ltc_get_bcd_tens_isr(40, 3);

    uint8_t hour_units = ltc_get_bcd_units_isr(48);
    uint8_t hour_tens  = ltc_get_bcd_tens_isr(56, 2);

    uint8_t frames = (uint8_t)(frame_units + frame_tens * 10);
    uint8_t seconds = (uint8_t)(sec_units + sec_tens * 10);
    uint8_t minutes = (uint8_t)(min_units + min_tens * 10);
    uint8_t hours = (uint8_t)(hour_units + hour_tens * 10);

    // Zatím očekáváme 25 fps.
    bool valid =
        (frame_units <= 9) &&
        (frame_tens <= 2) &&
        (frames < 25) &&
        (sec_units <= 9) &&
        (sec_tens <= 5) &&
        (seconds < 60) &&
        (min_units <= 9) &&
        (min_tens <= 5) &&
        (minutes < 60) &&
        (hour_units <= 9) &&
        (hour_tens <= 2) &&
        (hours < 24);

    if (valid) {
        s_tc_frames = frames;
        s_tc_seconds = seconds;
        s_tc_minutes = minutes;
        s_tc_hours = hours;
        s_tc_valid = true;
        s_last_valid_frame_time_us = s_last_edge_time_us;
        s_decoded_count++;
        s_format_error = false;
    } else {
        s_tc_valid = false;
        s_decode_error_count++;
        ltc_mark_format_error_isr();
    }
}

static void IRAM_ATTR ltc_process_bit_isr(uint8_t bit)
{
    bit &= 0x01;

    ltc_store_bit_isr(bit);

    s_bit_count++;
    s_bits_since_sync++;

    if (bit) {
        s_one_count++;
    } else {
        s_zero_count++;
    }

    s_sync_shift_reg = (uint16_t)((s_sync_shift_reg << 1) | bit);

    if (s_sync_shift_reg == LTC_SYNC_3FFD) {
        s_sync_count++;
        s_sync_3ffd_count++;
        s_bits_since_sync = 0;
        ltc_decode_frame_isr();
    } else if (s_sync_shift_reg == LTC_SYNC_BFFC) {
        s_sync_count++;
        s_sync_bffc_count++;
        s_bits_since_sync = 0;
        ltc_decode_frame_isr();
    }
}

static void IRAM_ATTR ltc_process_interval_isr(uint32_t interval_us)
{
    if (interval_us >= LTC_SHORT_MIN_US && interval_us <= LTC_SHORT_MAX_US) {
        s_short_count++;

        if (s_pending_short) {
            // short + short = bit 1
            ltc_process_bit_isr(1);
            s_pending_short = false;
        } else {
            // první půlka bitu 1
            s_pending_short = true;
        }

    } else if (interval_us >= LTC_LONG_MIN_US && interval_us <= LTC_LONG_MAX_US) {
        s_long_count++;

        if (s_pending_short) {
            // long po samotném shortu by u čistého BMC neměl přijít
            s_bit_error_count++;
            ltc_mark_format_error_isr();
            s_pending_short = false;
        }

        // long = bit 0
        ltc_process_bit_isr(0);

    } else {
        s_invalid_count++;
        ltc_mark_format_error_isr();

        if (s_pending_short) {
            s_bit_error_count++;
            ltc_mark_format_error_isr();
            s_pending_short = false;
        }
    }
}

static void IRAM_ATTR ltc_gpio_isr_handler(void *arg)
{
    (void)arg;

    int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL_ISR(&s_ltc_mux);

    if (s_last_edge_time_us != 0) {
        uint32_t interval_us = (uint32_t)(now_us - s_last_edge_time_us);

        s_last_interval_us = interval_us;

        if (interval_us < s_min_interval_us) {
            s_min_interval_us = interval_us;
        }

        if (interval_us > s_max_interval_us) {
            s_max_interval_us = interval_us;
        }

        ltc_process_interval_isr(interval_us);
    }

    s_last_edge_time_us = now_us;
    s_edges_total++;

    portEXIT_CRITICAL_ISR(&s_ltc_mux);
}

esp_err_t ltc_input_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    gpio_config_t gpio_cfg = {
        .pin_bit_mask = (1ULL << LTC_INPUT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    esp_err_t ret = gpio_config(&gpio_cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpio_install_isr_service(0);

    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = gpio_isr_handler_add(LTC_INPUT_GPIO, ltc_gpio_isr_handler, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    ltc_input_reset_stats();

    s_ready = true;

    if (debug_control_is_enabled()) {
        printf("LTC input initialized on GPIO4\n");
    }

    return ESP_OK;
}

void ltc_input_reset_stats(void)
{
    portENTER_CRITICAL(&s_ltc_mux);

    s_edges_total = 0;

    s_short_count = 0;
    s_long_count = 0;
    s_invalid_count = 0;

    s_bit_count = 0;
    s_zero_count = 0;
    s_one_count = 0;
    s_bit_error_count = 0;

    s_sync_count = 0;
    s_sync_3ffd_count = 0;
    s_sync_bffc_count = 0;

    s_decoded_count = 0;
    s_decode_error_count = 0;

    s_bits_since_sync = 0;
    s_sync_shift_reg = 0;

    s_pending_short = false;

    s_last_edge_time_us = 0;
    s_last_interval_us = 0;
    s_min_interval_us = UINT32_MAX;
    s_max_interval_us = 0;

    for (uint8_t i = 0; i < LTC_FRAME_BITS; i++) {
        s_bit_ring[i] = 0;
    }

    s_bit_write_pos = 0;
    s_bits_collected = 0;

    s_tc_valid = false;
    s_tc_hours = 0;
    s_tc_minutes = 0;
    s_tc_seconds = 0;
    s_tc_frames = 0;
    s_last_valid_frame_time_us = 0;
    s_format_error = false;
    s_last_format_error_time_us = 0;

    portEXIT_CRITICAL(&s_ltc_mux);
}

void ltc_input_get_stats(ltc_input_stats_t *stats)
{
    if (!stats) {
        return;
    }

    int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&s_ltc_mux);

    uint64_t edges_total = s_edges_total;

    uint64_t short_count = s_short_count;
    uint64_t long_count = s_long_count;
    uint64_t invalid_count = s_invalid_count;

    uint64_t bit_count = s_bit_count;
    uint64_t zero_count = s_zero_count;
    uint64_t one_count = s_one_count;
    uint64_t bit_error_count = s_bit_error_count;

    uint64_t sync_count = s_sync_count;
    uint64_t sync_3ffd_count = s_sync_3ffd_count;
    uint64_t sync_bffc_count = s_sync_bffc_count;

    uint64_t decoded_count = s_decoded_count;
    uint64_t decode_error_count = s_decode_error_count;

    uint32_t bits_since_sync = s_bits_since_sync;
    uint16_t sync_shift_reg = s_sync_shift_reg;

    bool tc_valid = s_tc_valid;
    int64_t last_valid_frame_time_us = s_last_valid_frame_time_us;
    uint8_t tc_hours = s_tc_hours;
    uint8_t tc_minutes = s_tc_minutes;
    uint8_t tc_seconds = s_tc_seconds;
    uint8_t tc_frames = s_tc_frames;

    bool pending_short = s_pending_short;
    bool format_error = s_format_error;
    int64_t last_format_error_time_us = s_last_format_error_time_us;

    int64_t last_edge_time_us = s_last_edge_time_us;
    uint32_t last_interval_us = s_last_interval_us;
    uint32_t min_interval_us = s_min_interval_us;
    uint32_t max_interval_us = s_max_interval_us;

    portEXIT_CRITICAL(&s_ltc_mux);

    stats->edges_total = edges_total;

    stats->short_count = short_count;
    stats->long_count = long_count;
    stats->invalid_count = invalid_count;

    stats->bit_count = bit_count;
    stats->zero_count = zero_count;
    stats->one_count = one_count;
    stats->bit_error_count = bit_error_count;

    stats->sync_count = sync_count;
    stats->sync_3ffd_count = sync_3ffd_count;
    stats->sync_bffc_count = sync_bffc_count;

    stats->decoded_count = decoded_count;
    stats->decode_error_count = decode_error_count;

    stats->bits_since_sync = bits_since_sync;
    stats->sync_shift_reg = sync_shift_reg;

    if (tc_valid) {
        if (last_valid_frame_time_us <= 0 ||
            (now_us - last_valid_frame_time_us) > LTC_VALID_TIMEOUT_US) {
            tc_valid = false;
        }
    }

    if (format_error) {
        if (last_format_error_time_us <= 0 ||
            (now_us - last_format_error_time_us) > LTC_VALID_TIMEOUT_US) {
            format_error = false;
        }
    }

    stats->tc_valid = tc_valid;
    stats->format_error = (!tc_valid && format_error);
    stats->tc_hours = tc_hours;
    stats->tc_minutes = tc_minutes;
    stats->tc_seconds = tc_seconds;
    stats->tc_frames = tc_frames;

    stats->pending_short = pending_short;

    stats->last_interval_us = last_interval_us;
    stats->min_interval_us = (min_interval_us == UINT32_MAX) ? 0 : min_interval_us;
    stats->max_interval_us = max_interval_us;
    stats->level = gpio_get_level(LTC_INPUT_GPIO);
    stats->signal_seen = (edges_total > 0);

    if (last_edge_time_us == 0) {
        stats->silence_us = 0;
    } else {
        stats->silence_us = (uint32_t)(now_us - last_edge_time_us);
    }
}

bool ltc_input_get_time(ltc_input_time_t *time)
{
    if (!time) {
        return false;
    }

    portENTER_CRITICAL(&s_ltc_mux);

    bool valid = s_tc_valid;
    int64_t last_valid_frame_time_us = s_last_valid_frame_time_us;
    uint8_t hours = s_tc_hours;
    uint8_t minutes = s_tc_minutes;
    uint8_t seconds = s_tc_seconds;
    uint8_t frames = s_tc_frames;
    bool format_error = s_format_error;
    int64_t last_format_error_time_us = s_last_format_error_time_us;

    portEXIT_CRITICAL(&s_ltc_mux);

    int64_t now_us = esp_timer_get_time();

    if (valid) {

        if (last_valid_frame_time_us <= 0 ||
            (now_us - last_valid_frame_time_us) > LTC_VALID_TIMEOUT_US) {
            valid = false;

            portENTER_CRITICAL(&s_ltc_mux);
            s_tc_valid = false;
            portEXIT_CRITICAL(&s_ltc_mux);
        }
    }

    if (format_error) {
        if (last_format_error_time_us <= 0 ||
            (now_us - last_format_error_time_us) > LTC_VALID_TIMEOUT_US) {
            format_error = false;
        }
    }

    time->hours = hours;
    time->minutes = minutes;
    time->seconds = seconds;
    time->frames = frames;
    time->valid = valid;
    time->format_error = (!valid && format_error);

    return valid;
}

void ltc_input_print_debug(void)
{
    static uint64_t last_edges_total = 0;
    static uint64_t last_bit_count = 0;
    static uint64_t last_sync_count = 0;
    static int64_t last_print_time_us = 0;

    ltc_input_stats_t stats;
    ltc_input_get_stats(&stats);

    int64_t now_us = esp_timer_get_time();

    uint32_t edges_per_sec = 0;
    uint32_t bits_per_sec = 0;
    uint32_t frames_per_sec = 0;

    if (last_print_time_us != 0) {
        uint64_t delta_edges = stats.edges_total - last_edges_total;
        uint64_t delta_bits = stats.bit_count - last_bit_count;
        uint64_t delta_sync = stats.sync_count - last_sync_count;
        int64_t delta_time_us = now_us - last_print_time_us;

        if (delta_time_us > 0) {
            edges_per_sec = (uint32_t)((delta_edges * 1000000ULL) / (uint64_t)delta_time_us);
            bits_per_sec = (uint32_t)((delta_bits * 1000000ULL) / (uint64_t)delta_time_us);
            frames_per_sec = (uint32_t)((delta_sync * 1000000ULL) / (uint64_t)delta_time_us);
        }
    }

    last_edges_total = stats.edges_total;
    last_bit_count = stats.bit_count;
    last_sync_count = stats.sync_count;
    last_print_time_us = now_us;

    printf(
        "LTC GPIO4: "
        "tc=%02u:%02u:%02u:%02u valid=%d "
        "edges/s=%" PRIu32
        " bits/s=%" PRIu32
        " frames/s=%" PRIu32
        " sync=%" PRIu64
        " dec=%" PRIu64
        " dec_err=%" PRIu64
        " since=%" PRIu32
        " reg=0x%04X"
        " invalid=%" PRIu64
        " bit_err=%" PRIu64
        " last=%" PRIu32 "us"
        " min=%" PRIu32 "us"
        " max=%" PRIu32 "us"
        " silence=%" PRIu32 "us"
        " level=%d\n",
        stats.tc_hours,
        stats.tc_minutes,
        stats.tc_seconds,
        stats.tc_frames,
        stats.tc_valid ? 1 : 0,
        edges_per_sec,
        bits_per_sec,
        frames_per_sec,
        stats.sync_count,
        stats.decoded_count,
        stats.decode_error_count,
        stats.bits_since_sync,
        (unsigned int)stats.sync_shift_reg,
        stats.invalid_count,
        stats.bit_error_count,
        stats.last_interval_us,
        stats.min_interval_us,
        stats.max_interval_us,
        stats.silence_us,
        stats.level
    );
}