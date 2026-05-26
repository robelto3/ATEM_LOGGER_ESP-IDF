#include "logger_events.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "app_state.h"
#include "cut_event.h"
#include "debug_control.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "logger_session.h"
#include "rtc.h"

#define LOGGER_EVENTS_TASK_NAME       "logger_events"
#define LOGGER_EVENTS_TASK_STACK_SIZE 8192
#define LOGGER_EVENTS_TASK_PRIORITY   6
#define LOGGER_EVENTS_TASK_CORE       0

// Logger task je pomalejší část projektu:
// zpracování fronty, CUT eventy, nový EDL soubor a zápisy na SD.
// Je připnutý na Core 0, aby ATEM task mohl běžet odděleně na Core 1.

typedef enum {
    LOGGER_EVENT_PROGRAM_SYNC = 1,
    LOGGER_EVENT_PROGRAM_CUT,
    LOGGER_EVENT_NEW_FILE,
} logger_event_type_t;

typedef struct {
    logger_event_type_t type;
    uint8_t old_program_input;
    uint8_t new_program_input;
    ltc_time_t tc;
    bool tc_valid;
    uint64_t queued_us;
} logger_event_t;

static QueueHandle_t s_queue = NULL;
static TaskHandle_t s_task_handle = NULL;
static portMUX_TYPE s_stats_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_dropped_count = 0;

static void logger_events_inc_dropped(void)
{
    portENTER_CRITICAL(&s_stats_mux);
    s_dropped_count++;
    portEXIT_CRITICAL(&s_stats_mux);
}

uint32_t logger_events_get_dropped_count(void)
{
    uint32_t count;

    portENTER_CRITICAL(&s_stats_mux);
    count = s_dropped_count;
    portEXIT_CRITICAL(&s_stats_mux);

    return count;
}

uint32_t logger_events_get_waiting_count(void)
{
    if (s_queue == NULL) {
        return 0;
    }

    return (uint32_t)uxQueueMessagesWaiting(s_queue);
}

static esp_err_t logger_events_submit(const logger_event_t *event)
{
    if ((s_queue == NULL) || (event == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t ret = xQueueSend(s_queue, event, 0);
    if (ret != pdPASS) {
        logger_events_inc_dropped();
        if (debug_control_is_enabled()) {
            printf(
                "LOGGER EVENTS: queue full, event dropped type=%u dropped=%lu\n",
                (unsigned)event->type,
                (unsigned long)logger_events_get_dropped_count()
            );
        }
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static logger_event_t logger_events_make_event(logger_event_type_t type,
                                               uint8_t old_program_input,
                                               uint8_t new_program_input)
{
    app_state_snapshot_t state;
    app_state_get_snapshot(&state);

    logger_event_t event = {
        .type = type,
        .old_program_input = old_program_input,
        .new_program_input = new_program_input,
        .tc = state.tc,
        .tc_valid = state.ltc_valid,
        .queued_us = (uint64_t)esp_timer_get_time(),
    };

    return event;
}

esp_err_t logger_events_submit_program_sync(uint8_t program_input)
{
    logger_event_t event = logger_events_make_event(LOGGER_EVENT_PROGRAM_SYNC, 0, program_input);

    return logger_events_submit(&event);
}

esp_err_t logger_events_submit_program_cut(uint8_t old_program_input, uint8_t new_program_input)
{
    logger_event_t event = logger_events_make_event(
        LOGGER_EVENT_PROGRAM_CUT,
        old_program_input,
        new_program_input
    );

    return logger_events_submit(&event);
}

esp_err_t logger_events_submit_new_file(void)
{
    logger_event_t event = logger_events_make_event(LOGGER_EVENT_NEW_FILE, 0, 0);

    return logger_events_submit(&event);
}

static void logger_events_handle_new_file(const logger_event_t *event)
{
    rtc_datetime_t rtc_now = {0};
    esp_err_t rtc_ret = rtc_read_datetime(&rtc_now);
    app_state_update_rtc(&rtc_now, rtc_ret == ESP_OK);

    // Ukončení aktuální EDL session:
    // 1) pokud existuje rozpracovaný segment, uzavře se aktuálním TC do starého souboru,
    // 2) číslování a RAM segmenty se vynulují,
    // 3) založí se nový EDL soubor.
    if (event != NULL) {
        (void)cut_event_close_active_segment_at_tc(&event->tc, event->tc_valid);
    } else {
        (void)cut_event_close_active_segment_from_state();
    }
    cut_event_reset_session();

    esp_err_t session_ret = logger_session_start_new_from_rtc(&rtc_now, rtc_ret == ESP_OK);
    if ((session_ret != ESP_OK) && debug_control_is_enabled()) {
        printf("LOGGER SESSION: new file FAILED: %s\n", esp_err_to_name(session_ret));
    }
}

static void logger_events_process_one(const logger_event_t *event)
{
    if (event == NULL) {
        return;
    }

    if (debug_control_is_enabled()) {
        uint64_t now_us = (uint64_t)esp_timer_get_time();
        uint32_t delay_us = (now_us >= event->queued_us) ? (uint32_t)(now_us - event->queued_us) : 0U;

        if (event->type == LOGGER_EVENT_PROGRAM_CUT) {
            printf(
                "LOGGER EVENTS: CUT queued delay=%lu us PGM %u -> %u\n",
                (unsigned long)delay_us,
                (unsigned)event->old_program_input,
                (unsigned)event->new_program_input
            );
        } else if (event->type == LOGGER_EVENT_PROGRAM_SYNC) {
            printf(
                "LOGGER EVENTS: SYNC queued delay=%lu us PGM %u\n",
                (unsigned long)delay_us,
                (unsigned)event->new_program_input
            );
        } else if (event->type == LOGGER_EVENT_NEW_FILE) {
            printf(
                "LOGGER EVENTS: NEW FILE queued delay=%lu us\n",
                (unsigned long)delay_us
            );
        }
    }

    switch (event->type) {
        case LOGGER_EVENT_PROGRAM_SYNC:
            cut_event_sync_program_start_at_tc(
                event->new_program_input,
                &event->tc,
                event->tc_valid
            );
            break;

        case LOGGER_EVENT_PROGRAM_CUT:
            cut_event_record_with_previous_at_tc(
                event->old_program_input,
                event->new_program_input,
                &event->tc,
                event->tc_valid
            );
            break;

        case LOGGER_EVENT_NEW_FILE:
            logger_events_handle_new_file(event);
            break;

        default:
            break;
    }
}

static void logger_events_task(void *arg)
{
    (void)arg;

    logger_event_t event;

    while (1) {
        if (xQueueReceive(s_queue, &event, portMAX_DELAY) == pdPASS) {
            logger_events_process_one(&event);
        }
    }
}

esp_err_t logger_events_init(void)
{
    if (s_task_handle != NULL) {
        return ESP_OK;
    }

    if (s_queue == NULL) {
        s_queue = xQueueCreate(LOGGER_EVENTS_QUEUE_LENGTH, sizeof(logger_event_t));
        if (s_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    BaseType_t ret = xTaskCreatePinnedToCore(
        logger_events_task,
        LOGGER_EVENTS_TASK_NAME,
        LOGGER_EVENTS_TASK_STACK_SIZE,
        NULL,
        LOGGER_EVENTS_TASK_PRIORITY,
        &s_task_handle,
        LOGGER_EVENTS_TASK_CORE
    );

    if (ret != pdPASS) {
        s_task_handle = NULL;
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    if (debug_control_is_enabled()) {
        printf(
            "LOGGER EVENTS: queue started, length=%u, core=%d\n",
            (unsigned)LOGGER_EVENTS_QUEUE_LENGTH,
            LOGGER_EVENTS_TASK_CORE
        );
    }

    return ESP_OK;
}
