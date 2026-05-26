#include "app_tasks.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "app_state.h"
#include "debug_control.h"
#include "display.h"
#include "esp_err.h"
#include "fake_cut_button.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger_events.h"
#include "ltc.h"
#include "new_file_button.h"
#include "rtc.h"
#include "tally_outputs.h"

// =====================================================
// Rozdělení práce mezi jádra
// =====================================================

#define APP_TASK_CORE_SLOW       0
#define APP_TASK_CORE_FAST       1

// Rychlá taska:
// - obnovuje aktuální LTC/TCx2 snapshot v app_state,
// - čte tlačítka GPIO46 a GPIO5,
// - při události pouze rychle vloží požadavek do logger_events fronty.
#define APP_FAST_TASK_NAME       "app_fast"
#define APP_FAST_TASK_STACK_SIZE 4096
#define APP_FAST_TASK_PRIORITY   6
#define APP_FAST_TASK_CORE       APP_TASK_CORE_FAST
#define APP_FAST_LOOP_DELAY_MS   10

// Pomalá/obslužná taska:
// - čte RTC,
// - obnovuje tally výstupy,
// - obnovuje OLED,
// - dělá periodické debug výpisy.
#define APP_UI_TASK_NAME         "app_ui"
#define APP_UI_TASK_STACK_SIZE   4096
#define APP_UI_TASK_PRIORITY     3
#define APP_UI_TASK_CORE         APP_TASK_CORE_SLOW
#define APP_UI_LOOP_DELAY_MS     50
#define DISPLAY_REFRESH_DIVIDER  4    // 4 cykly po 50 ms = 200 ms = 5x za sekundu
#define DEBUG_PRINT_DIVIDER      20   // 20 cyklů po 50 ms = 1x za sekundu

// =====================================================
// Debug nastavení
// =====================================================

#define DEBUG_STATE_PRINT        1
#define DEBUG_LTC_PRINT          1
#define DEBUG_FAKE_CUT_PRINT     1
#define DEBUG_NEW_FILE_PRINT     1

static TaskHandle_t s_fast_task_handle = NULL;
static TaskHandle_t s_ui_task_handle = NULL;

// =====================================================
// Pomocné funkce
// =====================================================

static void print_state_debug(void)
{
#if DEBUG_STATE_PRINT
    if (!debug_control_is_enabled()) {
        return;
    }

    app_state_snapshot_t state;
    app_state_get_snapshot(&state);

    if (state.rtc_valid) {
        printf(
            "RTC: %02u.%02u.%04u %02u:%02u:%02u  ",
            state.rtc.date,
            state.rtc.month,
            state.rtc.year,
            state.rtc.hours,
            state.rtc.minutes,
            state.rtc.seconds
        );
    } else {
        printf("RTC: ---  ");
    }

    printf(
        "TCx2: %02u:%02u:%02u:%02u valid=%d  ATEM=%d PGM=%u PVW=%u FILE=%s  Q=%lu/%u dropped=%lu ui_core=%d\n",
        state.tc.hours,
        state.tc.minutes,
        state.tc.seconds,
        state.tc.frames,
        state.ltc_valid ? 1 : 0,
        state.atem_connected ? 1 : 0,
        state.program_input,
        state.preview_input,
        state.current_filename,
        (unsigned long)logger_events_get_waiting_count(),
        (unsigned)LOGGER_EVENTS_QUEUE_LENGTH,
        (unsigned long)logger_events_get_dropped_count(),
        xPortGetCoreID()
    );
#endif
}

static void print_ltc_debug(void)
{
#if DEBUG_LTC_PRINT
    if (!debug_control_is_enabled()) {
        return;
    }

    ltc_print_debug();
#endif
}

static void handle_fake_cut_button(void)
{
    static uint8_t fake_program_input = 0;

    if (!fake_cut_button_was_pressed()) {
        return;
    }

    app_state_snapshot_t state;
    app_state_get_snapshot(&state);

    fake_program_input++;
    if (fake_program_input > 8) {
        fake_program_input = 1;
    }

#if DEBUG_FAKE_CUT_PRINT
    if (debug_control_is_enabled()) {
        printf("FAKE CUT GPIO%d pressed -> PGM %u\n", FAKE_CUT_BUTTON_GPIO, fake_program_input);
    }
#endif

    esp_err_t ret = logger_events_submit_program_cut(state.program_input, fake_program_input);
    if ((ret != ESP_OK) && debug_control_is_enabled()) {
        printf("FAKE CUT: enqueue FAILED: %s\n", esp_err_to_name(ret));
    }
}

static void handle_new_file_button(void)
{
    if (!new_file_button_was_pressed()) {
        return;
    }

#if DEBUG_NEW_FILE_PRINT
    if (debug_control_is_enabled()) {
        printf("NEW FILE GPIO%d pressed -> close current EDL and create next file\n", NEW_FILE_BUTTON_GPIO);
    }
#endif

    esp_err_t ret = logger_events_submit_new_file();
    if ((ret != ESP_OK) && debug_control_is_enabled()) {
        printf("NEW FILE: enqueue FAILED: %s\n", esp_err_to_name(ret));
    }
}

// =====================================================
// Tasky
// =====================================================

static void app_fast_task(void *arg)
{
    (void)arg;

    if (debug_control_is_enabled()) {
        printf("APP FAST: task started on core %d\n", APP_FAST_TASK_CORE);
    }

    while (1) {
        ltc_time_t ltc_now = {0};
        (void)ltc_get_time(&ltc_now);
        app_state_update_ltc(&ltc_now);

        handle_fake_cut_button();
        handle_new_file_button();

        vTaskDelay(pdMS_TO_TICKS(APP_FAST_LOOP_DELAY_MS));
    }
}

static void app_ui_task(void *arg)
{
    (void)arg;

    uint8_t display_divider = 0;
    uint8_t debug_divider = 0;

    if (debug_control_is_enabled()) {
        printf("APP UI: task started on core %d\n", APP_UI_TASK_CORE);
    }

    while (1) {
        tally_outputs_update();

        display_divider++;
        if (display_divider >= DISPLAY_REFRESH_DIVIDER) {
            display_divider = 0;

            rtc_datetime_t rtc_now = {0};
            esp_err_t rtc_ret = rtc_read_datetime(&rtc_now);
            app_state_update_rtc(&rtc_now, rtc_ret == ESP_OK);

            esp_err_t display_ret = display_show_main_screen();
            if ((display_ret != ESP_OK) && debug_control_is_enabled()) {
                printf("DISPLAY: refresh FAILED: %s\n", esp_err_to_name(display_ret));
            }
        }

        debug_divider++;
        if (debug_divider >= DEBUG_PRINT_DIVIDER) {
            debug_divider = 0;

            print_state_debug();
            print_ltc_debug();
        }

        vTaskDelay(pdMS_TO_TICKS(APP_UI_LOOP_DELAY_MS));
    }
}

// =====================================================
// Public API
// =====================================================

esp_err_t app_tasks_start(void)
{
    if ((s_fast_task_handle != NULL) || (s_ui_task_handle != NULL)) {
        return ESP_OK;
    }

    BaseType_t fast_ok = xTaskCreatePinnedToCore(
        app_fast_task,
        APP_FAST_TASK_NAME,
        APP_FAST_TASK_STACK_SIZE,
        NULL,
        APP_FAST_TASK_PRIORITY,
        &s_fast_task_handle,
        APP_FAST_TASK_CORE
    );

    if (fast_ok != pdPASS) {
        s_fast_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ui_ok = xTaskCreatePinnedToCore(
        app_ui_task,
        APP_UI_TASK_NAME,
        APP_UI_TASK_STACK_SIZE,
        NULL,
        APP_UI_TASK_PRIORITY,
        &s_ui_task_handle,
        APP_UI_TASK_CORE
    );

    if (ui_ok != pdPASS) {
        TaskHandle_t fast_handle = s_fast_task_handle;
        s_fast_task_handle = NULL;
        if (fast_handle != NULL) {
            vTaskDelete(fast_handle);
        }
        s_ui_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    if (debug_control_is_enabled()) {
        printf(
            "APP TASKS: started, fast core=%d, ui core=%d\n",
            APP_FAST_TASK_CORE,
            APP_UI_TASK_CORE
        );
    }

    return ESP_OK;
}
