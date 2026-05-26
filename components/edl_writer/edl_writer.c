#include "edl_writer.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "sd_storage.h"
#include "debug_control.h"

#define EDL_WRITER_PATH_MAX_LEN 96

// Soubor nenecháváme dlouhodobě otevřený.
// U FAT/VFS se při dlouho otevřeném souboru může přes web jevit velikost jako 0,
// protože adresářový záznam nemusí být hned aktualizovaný.
// Proto se session soubor při startu vytvoří/trunkuje a každý EDL zápis se otevře
// v append režimu, zapíše, flushne, fsyncne a zavře. Pro střihové eventy je to
// dostatečně rychlé a přes web je obsah ihned vidět.
static bool s_file_output_enabled = false;
static char s_edl_file_path[EDL_WRITER_PATH_MAX_LEN];

static void edl_writer_tc_to_string(const ltc_time_t *tc, char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }

    if (!tc) {
        snprintf(out, out_len, "--:--:--:--");
        return;
    }

    snprintf(
        out,
        out_len,
        "%02u:%02u:%02u:%02u",
        tc->hours,
        tc->minutes,
        tc->seconds,
        tc->frames
    );
}

static void edl_writer_append_vprintf(const char *fmt, va_list args)
{
    if (!s_file_output_enabled || s_edl_file_path[0] == '\0') {
        return;
    }

    FILE *file = fopen(s_edl_file_path, "a");
    if (file == NULL) {
        if (debug_control_is_enabled()) {
            printf("EDL SD: append FAILED: %s\n", s_edl_file_path);
        }
        return;
    }

    vfprintf(file, fmt, args);
    fflush(file);

    int fd = fileno(file);
    if (fd >= 0) {
        (void)fsync(fd);
    }

    fclose(file);
}

static void edl_writer_console_and_file(const char *fmt, ...)
{
    va_list console_args;
    va_list file_args;

    va_start(console_args, fmt);
    va_copy(file_args, console_args);

    if (debug_control_is_enabled()) {
        vprintf(fmt, console_args);
    }
    edl_writer_append_vprintf(fmt, file_args);

    va_end(file_args);
    va_end(console_args);
}

static void edl_writer_close_file(void)
{
    s_file_output_enabled = false;
}

static esp_err_t edl_writer_open_file(const char *filename)
{
    edl_writer_close_file();
    s_edl_file_path[0] = '\0';

    if (!filename || filename[0] == '\0') {
        if (debug_control_is_enabled()) {
            printf("EDL SD: open skipped, filename is empty\n");
        }
        return ESP_ERR_INVALID_ARG;
    }

    if (!sd_storage_is_mounted()) {
        if (debug_control_is_enabled()) {
            printf("EDL SD: open skipped, SD card is not mounted\n");
        }
        return ESP_ERR_INVALID_STATE;
    }

    int written = snprintf(
        s_edl_file_path,
        sizeof(s_edl_file_path),
        "%s/%s",
        SD_STORAGE_MOUNT_POINT,
        filename
    );

    if (written <= 0 || written >= (int)sizeof(s_edl_file_path)) {
        s_edl_file_path[0] = '\0';
        if (debug_control_is_enabled()) {
            printf("EDL SD: path too long\n");
        }
        return ESP_ERR_NO_MEM;
    }

    // Vytvoří nový prázdný soubor pro session. Hned ho zavřeme,
    // aby byl přes web vidět i adresářový záznam.
    FILE *file = fopen(s_edl_file_path, "w");
    if (file == NULL) {
        if (debug_control_is_enabled()) {
            printf("EDL SD: fopen write FAILED: %s\n", s_edl_file_path);
        }
        s_edl_file_path[0] = '\0';
        return ESP_FAIL;
    }

    fflush(file);
    int fd = fileno(file);
    if (fd >= 0) {
        (void)fsync(fd);
    }
    fclose(file);

    s_file_output_enabled = true;
    if (debug_control_is_enabled()) {
        printf("EDL SD: opened %s\n", s_edl_file_path);
    }
    return ESP_OK;
}

esp_err_t edl_writer_init(void)
{
    edl_writer_close_file();
    memset(s_edl_file_path, 0, sizeof(s_edl_file_path));
    return ESP_OK;
}

void edl_writer_write_header(const char *filename,
                             const char *title,
                             const rtc_datetime_t *created_rtc,
                             bool rtc_valid)
{
    if (debug_control_is_enabled()) {
        printf("LOGGER SESSION START\n");
    }
    if (debug_control_is_enabled()) {
        printf("FILE: %s\n", filename ? filename : "---");
    }

    if (!rtc_valid || !created_rtc || !title) {
        if (debug_control_is_enabled()) {
            printf("EDL HEADER: RTC invalid - header not complete\n");
        }
        return;
    }

    esp_err_t open_ret = edl_writer_open_file(filename);
    if (open_ret != ESP_OK) {
        if (debug_control_is_enabled()) {
            printf("EDL SD: file output disabled for this session\n");
        }
    }

    edl_writer_console_and_file(
        "*CREATED: %02u.%02u.%04u %02u:%02u:%02u\n",
        created_rtc->date,
        created_rtc->month,
        created_rtc->year,
        created_rtc->hours,
        created_rtc->minutes,
        created_rtc->seconds
    );
    edl_writer_console_and_file("TITLE: %s\n", title);
    edl_writer_console_and_file("FCM: NON-DROP FRAME\n");
    edl_writer_console_and_file("\n");
}

void edl_writer_write_cut_debug(uint32_t cut_number,
                                uint8_t old_program_input,
                                uint8_t new_program_input,
                                const ltc_time_t *tc,
                                bool tc_valid)
{
    char tc_text[16];
    edl_writer_tc_to_string(tc, tc_text, sizeof(tc_text));

    // CUT debug jde pouze do monitoru, ne do finálního EDL souboru.
    if (!debug_control_is_enabled()) {
        return;
    }

    printf(
        "CUT %06lu  PGM %u -> %u  TC %s  valid=%d\n",
        (unsigned long)cut_number,
        old_program_input,
        new_program_input,
        tc_text,
        tc_valid ? 1 : 0
    );
}

void edl_writer_write_event(uint32_t event_number,
                            uint8_t camera,
                            const ltc_time_t *in_tc,
                            const ltc_time_t *out_tc)
{
    char in_text[16];
    char out_text[16];

    edl_writer_tc_to_string(in_tc, in_text, sizeof(in_text));
    edl_writer_tc_to_string(out_tc, out_text, sizeof(out_text));

    // CMX/NDF styl pro DaVinci Resolve:
    // 000001  CAM1     V     C        01:00:07:46 01:00:08:38 01:00:07:46 01:00:08:38
    edl_writer_console_and_file(
        "%06lu  CAM%u     V     C        %s %s %s %s\n",
        (unsigned long)event_number,
        camera,
        in_text,
        out_text,
        in_text,
        out_text
    );

    edl_writer_console_and_file("*FROM CLIP NAME:  CAM%u\n", camera);
    edl_writer_console_and_file("*SOURCE FILE: CAM%u\n", camera);
}

void edl_writer_write_segment_start(uint8_t camera,
                                    const ltc_time_t *in_tc)
{
    char in_text[16];
    edl_writer_tc_to_string(in_tc, in_text, sizeof(in_text));

    // Segment start je jen diagnostika do monitoru.
    // Do EDL souboru se zapisuje až dokončený event s IN i OUT.
    if (debug_control_is_enabled()) {
        printf("EDL START CAM%u IN %s\n", camera, in_text);
    }
}

void edl_writer_write_invalid_tc_notice(void)
{
    // Diagnostika pouze do monitoru.
    if (debug_control_is_enabled()) {
        printf("EDL: TC invalid - segment not changed\n");
    }
}

bool edl_writer_is_file_open(void)
{
    return s_file_output_enabled;
}

const char *edl_writer_get_file_path(void)
{
    return s_edl_file_path;
}
