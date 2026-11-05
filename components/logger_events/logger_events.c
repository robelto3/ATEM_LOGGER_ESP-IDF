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
#include "freertos/semphr.h"
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
    LOGGER_EVENT_EDL_START,
    LOGGER_EVENT_EDL_STOP,
} logger_event_type_t;

typedef struct {
    logger_event_type_t type;
    uint8_t old_program_input;
    uint8_t new_program_input;
    ltc_time_t tc;
    bool tc_valid;
    uint64_t queued_us;
    uint32_t request_id;
} logger_event_t;

static QueueHandle_t s_queue = NULL;
static TaskHandle_t s_task_handle = NULL;
static SemaphoreHandle_t s_request_mutex = NULL;
static portMUX_TYPE s_stats_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_request_state_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_dropped_count = 0;
static uint32_t s_next_request_id = 0;
static uint32_t s_last_completed_request_id = 0;
static esp_err_t s_last_completed_request_result = ESP_OK;

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
        .request_id = 0,
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
    app_state_snapshot_t state;
    app_state_get_snapshot(&state);
    logger_event_t event = logger_events_make_event(
        LOGGER_EVENT_NEW_FILE,
        0,
        state.program_input
    );

    return logger_events_submit(&event);
}

static esp_err_t logger_events_start_session(const logger_event_t *event, bool force_new)
{
    if (!force_new && logger_session_is_active()) {
        return ESP_OK;
    }

    rtc_datetime_t rtc_now = {0};
    esp_err_t rtc_ret = rtc_read_datetime(&rtc_now);
    app_state_update_rtc(&rtc_now, rtc_ret == ESP_OK);

    if (force_new && logger_session_is_active()) {
        if (event != NULL) {
            (void)cut_event_close_active_segment_at_tc(&event->tc, event->tc_valid);
        } else {
            (void)cut_event_close_active_segment_from_state();
        }
        esp_err_t stop_ret = logger_session_stop();
        if (stop_ret != ESP_OK) {
            return stop_ret;
        }
    }

    cut_event_reset_session();

    esp_err_t session_ret = logger_session_start_new_from_rtc(&rtc_now, rtc_ret == ESP_OK);
    if ((session_ret != ESP_OK) && debug_control_is_enabled()) {
        printf("LOGGER SESSION: new file FAILED: %s\n", esp_err_to_name(session_ret));
    }

    if ((session_ret == ESP_OK) && (event != NULL)) {
        cut_event_sync_program_start_at_tc(
            event->new_program_input,
            &event->tc,
            event->tc_valid
        );
    }

    return session_ret;
}

static esp_err_t logger_events_stop_session(const logger_event_t *event)
{
    if (logger_session_is_active()) {
        if (event != NULL) {
            (void)cut_event_close_active_segment_at_tc(&event->tc, event->tc_valid);
        } else {
            (void)cut_event_close_active_segment_from_state();
        }
    }

    cut_event_reset_session();
    return logger_session_stop();
}

static void logger_events_complete_request(uint32_t request_id, esp_err_t result)
{
    if (request_id == 0U) {
        return;
    }

    portENTER_CRITICAL(&s_request_state_mux);
    s_last_completed_request_id = request_id;
    s_last_completed_request_result = result;
    portEXIT_CRITICAL(&s_request_state_mux);
}

static esp_err_t logger_events_process_one(const logger_event_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
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
        } else if (event->type == LOGGER_EVENT_EDL_START) {
            printf("LOGGER EVENTS: EDL START queued delay=%lu us\n", (unsigned long)delay_us);
        } else if (event->type == LOGGER_EVENT_EDL_STOP) {
            printf("LOGGER EVENTS: EDL STOP queued delay=%lu us\n", (unsigned long)delay_us);
        }
    }

    switch (event->type) {
        case LOGGER_EVENT_PROGRAM_SYNC:
            if (logger_session_is_active()) {
                cut_event_sync_program_start_at_tc(
                    event->new_program_input,
                    &event->tc,
                    event->tc_valid
                );
            }
            return ESP_OK;

        case LOGGER_EVENT_PROGRAM_CUT:
            if (logger_session_is_active()) {
                cut_event_record_with_previous_at_tc(
                    event->old_program_input,
                    event->new_program_input,
                    &event->tc,
                    event->tc_valid
                );
            }
            return ESP_OK;

        case LOGGER_EVENT_NEW_FILE:
            return logger_events_start_session(event, true);

        case LOGGER_EVENT_EDL_START:
            return logger_events_start_session(event, false);

        case LOGGER_EVENT_EDL_STOP:
            return logger_events_stop_session(event);

        default:
            return ESP_ERR_INVALID_ARG;
    }
}

static void logger_events_task(void *arg)
{
    (void)arg;

    logger_event_t event;

    while (1) {
        if (xQueueReceive(s_queue, &event, portMAX_DELAY) == pdPASS) {
            esp_err_t result = logger_events_process_one(&event);
            logger_events_complete_request(event.request_id, result);
        }
    }
}

static uint32_t logger_events_allocate_request_id(void)
{
    uint32_t request_id;

    portENTER_CRITICAL(&s_request_state_mux);
    s_next_request_id++;
    if (s_next_request_id == 0U) {
        s_next_request_id = 1U;
    }
    request_id = s_next_request_id;
    portEXIT_CRITICAL(&s_request_state_mux);

    return request_id;
}

static bool logger_events_get_request_result(uint32_t request_id, esp_err_t *result)
{
    bool completed = false;

    portENTER_CRITICAL(&s_request_state_mux);
    if (s_last_completed_request_id == request_id) {
        completed = true;
        if (result != NULL) {
            *result = s_last_completed_request_result;
        }
    }
    portEXIT_CRITICAL(&s_request_state_mux);

    return completed;
}

static esp_err_t logger_events_request_and_wait(logger_event_type_t type, uint32_t timeout_ms)
{
    if ((s_queue == NULL) || (s_request_mutex == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (timeout_ms == 0U) {
        timeout_ms = 1U;
    }

    if (xSemaphoreTake(s_request_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    app_state_snapshot_t state;
    app_state_get_snapshot(&state);

    logger_event_t event = logger_events_make_event(type, 0, state.program_input);
    event.request_id = logger_events_allocate_request_id();

    esp_err_t ret = logger_events_submit(&event);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_request_mutex);
        return ret;
    }

    uint64_t deadline_us = (uint64_t)esp_timer_get_time() + ((uint64_t)timeout_ms * 1000ULL);
    while ((uint64_t)esp_timer_get_time() < deadline_us) {
        if (logger_events_get_request_result(event.request_id, &ret)) {
            xSemaphoreGive(s_request_mutex);
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    xSemaphoreGive(s_request_mutex);
    return ESP_ERR_TIMEOUT;
}

esp_err_t logger_events_request_edl_start(uint32_t timeout_ms)
{
    return logger_events_request_and_wait(LOGGER_EVENT_EDL_START, timeout_ms);
}

esp_err_t logger_events_request_edl_stop(uint32_t timeout_ms)
{
    return logger_events_request_and_wait(LOGGER_EVENT_EDL_STOP, timeout_ms);
}

esp_err_t logger_events_init(void)
{
    if (s_task_handle != NULL) {
        return ESP_OK;
    }

    if (s_request_mutex == NULL) {
        s_request_mutex = xSemaphoreCreateMutex();
        if (s_request_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
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
        vSemaphoreDelete(s_request_mutex);
        s_request_mutex = NULL;
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
