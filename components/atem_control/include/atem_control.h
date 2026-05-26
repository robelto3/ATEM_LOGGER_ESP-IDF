#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ATEM komponenta čte stav Program/Preview a změny Program busu posílá do fronty logger událostí.
// IP adresa ATEMu se bere z komponenty net_config.

#define ATEM_CONTROL_SWITCHER_PORT 9910
#define ATEM_CONTROL_LOCAL_PORT    50100

typedef struct {
    bool task_running;
    bool connected;
    bool initialized;
    uint16_t session_id;
    uint16_t last_remote_packet_id;
    uint16_t program_input;
    uint16_t preview_input;
    uint32_t packets_rx;
    uint32_t packets_tx;
    uint32_t commands_rx;
    uint32_t reconnects;
} atem_control_status_t;

esp_err_t atem_control_init(void);
void atem_control_get_status(atem_control_status_t *status);

#ifdef __cplusplus
}
#endif
