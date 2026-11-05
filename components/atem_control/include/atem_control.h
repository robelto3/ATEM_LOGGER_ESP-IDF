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
#define ATEM_CONTROL_PRODUCT_NAME_LEN 45

typedef enum {
    ATEM_VIDEO_MODE_720P50 = 4,
    ATEM_VIDEO_MODE_720P5994 = 5,
    ATEM_VIDEO_MODE_1080I50 = 6,
    ATEM_VIDEO_MODE_1080I5994 = 7,
    ATEM_VIDEO_MODE_1080P2398 = 8,
    ATEM_VIDEO_MODE_1080P24 = 9,
    ATEM_VIDEO_MODE_1080P25 = 10,
    ATEM_VIDEO_MODE_1080P2997 = 11,
    ATEM_VIDEO_MODE_1080P50 = 12,
    ATEM_VIDEO_MODE_1080P5994 = 13,
    ATEM_VIDEO_MODE_UHDP2398 = 14,
    ATEM_VIDEO_MODE_UHDP24 = 15,
    ATEM_VIDEO_MODE_UHDP25 = 16,
    ATEM_VIDEO_MODE_UHDP2997 = 17,
    ATEM_VIDEO_MODE_UHDP50 = 18,
    ATEM_VIDEO_MODE_UHDP5994 = 19,
} atem_video_mode_t;

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
    char product_name[ATEM_CONTROL_PRODUCT_NAME_LEN];
    uint16_t protocol_version_major;
    uint16_t protocol_version_minor;
    bool video_mode_valid;
    uint8_t video_mode;
    bool video_modes_valid;
    uint32_t supported_video_modes;
} atem_control_status_t;

esp_err_t atem_control_init(void);
void atem_control_get_status(atem_control_status_t *status);
const char *atem_control_video_mode_name(uint8_t video_mode);
bool atem_control_video_mode_from_name(const char *name, uint8_t *video_mode);
bool atem_control_video_mode_is_supported(const atem_control_status_t *status, uint8_t video_mode);
esp_err_t atem_control_set_video_mode(uint8_t video_mode, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
