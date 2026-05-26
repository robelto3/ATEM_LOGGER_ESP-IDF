#include "atem_control.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_state.h"
#include "logger_events.h"
#include "debug_control.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "net_config.h"

#define ATEM_TASK_NAME           "atem_control"
#define ATEM_TASK_STACK_SIZE     4096
#define ATEM_TASK_PRIORITY       5
#define ATEM_TASK_CORE           1

#define ATEM_PACKET_MAX_LEN      1500
#define ATEM_HEADER_LEN          12
#define ATEM_INIT_PACKET_COUNT   40

#define ATEM_ACK_REQUEST         0x01
#define ATEM_HELLO_PACKET        0x02
#define ATEM_RESEND              0x04
#define ATEM_REQUEST_NEXT_AFTER  0x08
#define ATEM_ACK                 0x10

#define ATEM_CONNECT_RETRY_MS    1000U
#define ATEM_TIMEOUT_MS          5000U
#define ATEM_SOCKET_TIMEOUT_MS   50U

// Živé pakety z ATEMu jsou směrodatné pro Program/Preview.
// Diagnostika je po ověření parseru vypnutá, aby monitor nebyl zahlcený.
#define ATEM_DEBUG_ALL_COMMANDS 0
#define ATEM_DEBUG_PACKET_HEADERS 0
#define ATEM_DEBUG_SCAN_KNOWN_COMMANDS 0
#define ATEM_DEBUG_PAYLOAD_DUMP 0
#define ATEM_DEBUG_MAX_PACKET_LOGS 0
#define ATEM_DEBUG_MAX_COMMAND_LOGS 0
#define ATEM_DEBUG_MAX_PAYLOAD_DUMPS 0

#if ATEM_DEBUG_PACKET_HEADERS
static uint32_t s_debug_packet_logs = 0;
#endif

#if ATEM_DEBUG_ALL_COMMANDS
static uint32_t s_debug_command_logs = 0;
#endif

#if ATEM_DEBUG_PAYLOAD_DUMP || ATEM_DEBUG_SCAN_KNOWN_COMMANDS
static uint32_t s_debug_payload_dumps = 0;
#endif

static const char *TAG = "ATEM";

static TaskHandle_t s_task_handle = NULL;
static portMUX_TYPE s_status_mux = portMUX_INITIALIZER_UNLOCKED;
static atem_control_status_t s_status;

static int s_sock = -1;
static struct sockaddr_in s_atem_addr;
static char s_atem_ip_string[NET_CONFIG_IP_STR_LEN] = NET_CONFIG_DEFAULT_ATEM_IP;

static uint16_t s_session_id = 0x53AB;
static uint16_t s_local_packet_id = 0;
static uint16_t s_last_remote_packet_id = 0;

static bool s_connected = false;
static bool s_initialized = false;
static bool s_init_payload_sent = false;
static bool s_waiting_for_resend = false;
static bool s_program_baseline_valid = false;
static uint8_t s_init_payload_sent_at_packet_id = ATEM_INIT_PACKET_COUNT;
static bool s_missing_init_packets[ATEM_INIT_PACKET_COUNT];

static uint64_t s_last_contact_ms = 0;
static uint64_t s_last_connect_try_ms = 0;

// RX a TX musí být oddělené. ACK nesmí přepsat přijatý packet dřív,
// než z něj parser přečte ATEM commandy.
static uint8_t s_packet[ATEM_PACKET_MAX_LEN];
static uint8_t s_tx_packet[ATEM_PACKET_MAX_LEN];

static void atem_parse_command(const char cmd[5], const uint8_t *payload, uint16_t payload_len);

static uint64_t atem_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

static uint16_t atem_read_u16_be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void atem_debug_dump_packet_payload(uint16_t packet_length)
{
#if ATEM_DEBUG_PAYLOAD_DUMP
    if (s_debug_payload_dumps >= ATEM_DEBUG_MAX_PAYLOAD_DUMPS) {
        return;
    }

    uint16_t dump_len = packet_length;
    if (dump_len > 96U) {
        dump_len = 96U;
    }

    char line[3U * 16U + 1U];
    for (uint16_t base = 0; base < dump_len; base = (uint16_t)(base + 16U)) {
        uint16_t line_len = (uint16_t)(dump_len - base);
        if (line_len > 16U) {
            line_len = 16U;
        }

        size_t pos = 0;
        for (uint16_t i = 0; i < line_len; i++) {
            int written = snprintf(
                &line[pos],
                sizeof(line) - pos,
                "%02X%s",
                (unsigned)s_packet[base + i],
                (i + 1U < line_len) ? " " : ""
            );
            if (written < 0) {
                break;
            }
            pos += (size_t)written;
            if (pos >= sizeof(line)) {
                pos = sizeof(line) - 1U;
                break;
            }
        }
        line[pos] = '\0';

        ESP_LOGI(TAG, "dump %03u: %s", (unsigned)base, line);
    }

    s_debug_payload_dumps++;
#else
    (void)packet_length;
#endif
}

#if ATEM_DEBUG_SCAN_KNOWN_COMMANDS
static bool atem_command_name_matches(const uint8_t *p, const char *name)
{
    return (p[0] == (uint8_t)name[0]) &&
           (p[1] == (uint8_t)name[1]) &&
           (p[2] == (uint8_t)name[2]) &&
           (p[3] == (uint8_t)name[3]);
}

static void atem_scan_for_known_commands(uint16_t packet_length)
{
    static const char *known_cmds[] = {
        "PrgI",
        "PrvI",
        "_Prg",
        "_Prv",
    };

    bool found_any = false;

    for (uint16_t offset = 0; (offset + 4U) <= packet_length; offset++) {
        for (size_t k = 0; k < (sizeof(known_cmds) / sizeof(known_cmds[0])); k++) {
            if (!atem_command_name_matches(&s_packet[offset], known_cmds[k])) {
                continue;
            }

            found_any = true;
            uint16_t block_start = offset >= 4U ? (uint16_t)(offset - 4U) : offset;
            uint16_t cmd_length = 0;
            uint16_t payload_len = 0;
            const uint8_t *payload = &s_packet[offset + 4U];

            if (offset >= 4U) {
                cmd_length = atem_read_u16_be(&s_packet[offset - 4U]);
                if ((cmd_length >= 8U) && ((block_start + cmd_length) <= packet_length)) {
                    payload_len = (uint16_t)(cmd_length - 8U);
                } else if ((offset + 4U) < packet_length) {
                    payload_len = (uint16_t)(packet_length - (offset + 4U));
                }
            } else if ((offset + 4U) < packet_length) {
                payload_len = (uint16_t)(packet_length - (offset + 4U));
            }

            char cmd[5] = {
                (char)s_packet[offset],
                (char)s_packet[offset + 1U],
                (char)s_packet[offset + 2U],
                (char)s_packet[offset + 3U],
                '\0'
            };

            ESP_LOGI(
                TAG,
                "scan found cmd='%s' at offset=%u block_start=%u block_len=%u payload_len=%u",
                cmd,
                (unsigned)offset,
                (unsigned)block_start,
                (unsigned)cmd_length,
                (unsigned)payload_len
            );

            atem_parse_command(cmd, payload, payload_len);
        }
    }

    if (!found_any && s_debug_payload_dumps <= ATEM_DEBUG_MAX_PAYLOAD_DUMPS) {
        ESP_LOGI(TAG, "scan found no PrgI/PrvI/_Prg/_Prv in this packet");
    }
}


#endif

static void atem_status_set_connected(bool connected, bool initialized)
{
    portENTER_CRITICAL(&s_status_mux);
    s_status.connected = connected;
    s_status.initialized = initialized;
    s_status.session_id = s_session_id;
    s_status.last_remote_packet_id = s_last_remote_packet_id;
    portEXIT_CRITICAL(&s_status_mux);

    app_state_set_atem_connected(connected);
}

static void atem_status_set_program_preview(uint16_t program_input, uint16_t preview_input)
{
    portENTER_CRITICAL(&s_status_mux);
    s_status.program_input = program_input;
    s_status.preview_input = preview_input;
    portEXIT_CRITICAL(&s_status_mux);

    uint8_t pgm = program_input <= 255U ? (uint8_t)program_input : 0U;
    uint8_t pvw = preview_input <= 255U ? (uint8_t)preview_input : 0U;
    app_state_set_program_preview(pgm, pvw);
}

static void atem_status_inc_packets_rx(void)
{
    portENTER_CRITICAL(&s_status_mux);
    s_status.packets_rx++;
    portEXIT_CRITICAL(&s_status_mux);
}

static void atem_status_inc_packets_tx(void)
{
    portENTER_CRITICAL(&s_status_mux);
    s_status.packets_tx++;
    portEXIT_CRITICAL(&s_status_mux);
}

static void atem_status_inc_commands_rx(void)
{
    portENTER_CRITICAL(&s_status_mux);
    s_status.commands_rx++;
    portEXIT_CRITICAL(&s_status_mux);
}

static void atem_status_inc_reconnects(void)
{
    portENTER_CRITICAL(&s_status_mux);
    s_status.reconnects++;
    portEXIT_CRITICAL(&s_status_mux);
}

static void atem_clear_tx_packet(void)
{
    memset(s_tx_packet, 0, sizeof(s_tx_packet));
}

static void atem_create_header(uint8_t header_cmd, uint16_t length, uint16_t remote_packet_id)
{
    s_tx_packet[0] = (uint8_t)((header_cmd << 3) | ((length >> 8) & 0x07));
    s_tx_packet[1] = (uint8_t)(length & 0xFF);
    s_tx_packet[2] = (uint8_t)(s_session_id >> 8);
    s_tx_packet[3] = (uint8_t)(s_session_id & 0xFF);
    s_tx_packet[4] = (uint8_t)(remote_packet_id >> 8);
    s_tx_packet[5] = (uint8_t)(remote_packet_id & 0xFF);

    if ((header_cmd & (ATEM_HELLO_PACKET | ATEM_ACK | ATEM_REQUEST_NEXT_AFTER)) == 0U) {
        s_local_packet_id++;
        s_tx_packet[10] = (uint8_t)(s_local_packet_id >> 8);
        s_tx_packet[11] = (uint8_t)(s_local_packet_id & 0xFF);
    }
}

static esp_err_t atem_send_packet(uint16_t length)
{
    if (s_sock < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    ssize_t sent = sendto(
        s_sock,
        s_tx_packet,
        length,
        0,
        (const struct sockaddr *)&s_atem_addr,
        sizeof(s_atem_addr)
    );

    if (sent < 0 || sent != (ssize_t)length) {
        ESP_LOGW(TAG, "send failed: errno=%d", errno);
        return ESP_FAIL;
    }

    atem_status_inc_packets_tx();
    return ESP_OK;
}

static void atem_send_connect_packet(void)
{
    s_local_packet_id = 0;
    s_session_id = 0x53AB;
    s_connected = false;
    s_initialized = false;
    s_init_payload_sent = false;
    s_waiting_for_resend = false;
    s_program_baseline_valid = false;
    s_init_payload_sent_at_packet_id = ATEM_INIT_PACKET_COUNT;
    memset(s_missing_init_packets, true, sizeof(s_missing_init_packets));

    atem_clear_tx_packet();
    atem_create_header(ATEM_HELLO_PACKET, 20, 0);
    s_tx_packet[9] = 0x3A;
    s_tx_packet[12] = 0x01;

    ESP_LOGI(
        TAG,
        "Sending connect packet to ATEM %s:%d from local UDP port %d",
        s_atem_ip_string,
        ATEM_CONTROL_SWITCHER_PORT,
        ATEM_CONTROL_LOCAL_PORT
    );

    (void)atem_send_packet(20);
    s_last_connect_try_ms = atem_now_ms();
    atem_status_inc_reconnects();
    atem_status_set_connected(false, false);
}

static void atem_send_ack(uint16_t remote_packet_id)
{
    atem_clear_tx_packet();
    atem_create_header(ATEM_ACK, 12, remote_packet_id);
    (void)atem_send_packet(12);
}

static void atem_send_first_hello_ack(void)
{
    atem_clear_tx_packet();
    atem_create_header(ATEM_ACK, 12, 0);
    s_tx_packet[9] = 0x03;
    (void)atem_send_packet(12);
}

static uint16_t atem_decode_source_from_prgi_prvi_payload(const uint8_t *payload, uint16_t payload_len)
{
    if (!payload) {
        return 0;
    }

    // Novější ATEM protokol používá 16bit video source na payload[2..3].
    if (payload_len >= 4U) {
        return atem_read_u16_be(&payload[2]);
    }

    // Starší ATEM protokol používal jednobajtovou hodnotu na payload[1].
    if (payload_len >= 2U) {
        return payload[1];
    }

    return 0;
}

static void atem_parse_command(const char cmd[5], const uint8_t *payload, uint16_t payload_len)
{
    atem_status_inc_commands_rx();

    atem_control_status_t st;
    atem_control_get_status(&st);

#if ATEM_DEBUG_ALL_COMMANDS
    if (s_debug_command_logs < ATEM_DEBUG_MAX_COMMAND_LOGS) {
        uint8_t b0 = payload_len > 0U ? payload[0] : 0U;
        uint8_t b1 = payload_len > 1U ? payload[1] : 0U;
        uint8_t b2 = payload_len > 2U ? payload[2] : 0U;
        uint8_t b3 = payload_len > 3U ? payload[3] : 0U;
        uint8_t b4 = payload_len > 4U ? payload[4] : 0U;
        uint8_t b5 = payload_len > 5U ? payload[5] : 0U;
        uint8_t b6 = payload_len > 6U ? payload[6] : 0U;
        uint8_t b7 = payload_len > 7U ? payload[7] : 0U;
        ESP_LOGI(
            TAG,
            "cmd='%s' payload_len=%u data=%02X %02X %02X %02X %02X %02X %02X %02X",
            cmd,
            (unsigned)payload_len,
            (unsigned)b0,
            (unsigned)b1,
            (unsigned)b2,
            (unsigned)b3,
            (unsigned)b4,
            (unsigned)b5,
            (unsigned)b6,
            (unsigned)b7
        );
        s_debug_command_logs++;
    }
#endif

    if ((strcmp(cmd, "PrgI") == 0) || (strcmp(cmd, "_Prg") == 0)) {
        uint16_t pgm = atem_decode_source_from_prgi_prvi_payload(payload, payload_len);

        // První načtený Program po připojení/reconnectu je pouze výchozí stav.
        // Nechceme z něj dělat falešný CUT, ale založíme výchozí segment v RAM,
        // aby další reálná změna PGM uzavřela správnou kameru.
        if (!s_program_baseline_valid) {
            s_program_baseline_valid = true;
            if (debug_control_is_enabled()) {
                ESP_LOGI(TAG, "Program Bus initial: %u", (unsigned)pgm);
            }

            if (pgm > 0U && pgm <= 255U) {
                (void)logger_events_submit_program_sync((uint8_t)pgm);
            }

            atem_status_set_program_preview(pgm, st.preview_input);
            return;
        }

        if (pgm != st.program_input) {
            if (debug_control_is_enabled()) {
                ESP_LOGI(TAG, "Program Bus: %u -> %u", (unsigned)st.program_input, (unsigned)pgm);
            }

            if (pgm > 0U && pgm <= 255U) {
                (void)logger_events_submit_program_cut((uint8_t)st.program_input, (uint8_t)pgm);
            } else {
                ESP_LOGW(TAG, "Program Bus source %u is outside 1..255 - EDL event skipped", (unsigned)pgm);
            }
        }

        atem_status_set_program_preview(pgm, st.preview_input);
        return;
    }

    if ((strcmp(cmd, "PrvI") == 0) || (strcmp(cmd, "_Prv") == 0)) {
        uint16_t pvw = atem_decode_source_from_prgi_prvi_payload(payload, payload_len);
        if (pvw != st.preview_input && debug_control_is_enabled()) {
            ESP_LOGI(TAG, "Preview Bus: %u -> %u", (unsigned)st.preview_input, (unsigned)pvw);
        }
        atem_status_set_program_preview(st.program_input, pvw);
        return;
    }
}

static void atem_parse_payload(uint16_t packet_length)
{
    uint16_t offset = ATEM_HEADER_LEN;

    while ((offset + 8U) <= packet_length) {
        uint16_t cmd_length = atem_read_u16_be(&s_packet[offset]);

        // Některé pakety mohou mít na konci nulovou výplň. To není chyba příkazu,
        // jen konec užitečných dat v tomto UDP packetu.
        if (cmd_length == 0U) {
            return;
        }

        if (cmd_length < 8U) {
            ESP_LOGW(TAG, "bad command length %u at offset %u", (unsigned)cmd_length, (unsigned)offset);
            return;
        }

        if ((offset + cmd_length) > packet_length) {
            ESP_LOGW(
                TAG,
                "command outside packet: offset=%u cmd_len=%u pkt_len=%u",
                (unsigned)offset,
                (unsigned)cmd_length,
                (unsigned)packet_length
            );
            return;
        }

        char cmd[5] = {
            (char)s_packet[offset + 4U],
            (char)s_packet[offset + 5U],
            (char)s_packet[offset + 6U],
            (char)s_packet[offset + 7U],
            '\0'
        };

        const uint8_t *payload = &s_packet[offset + 8U];
        uint16_t payload_len = (uint16_t)(cmd_length - 8U);

        atem_parse_command(cmd, payload, payload_len);
        offset = (uint16_t)(offset + cmd_length);
    }
}

static void atem_process_packet(ssize_t packet_size)
{
    if (packet_size < ATEM_HEADER_LEN) {
        return;
    }

    uint16_t packet_length = (uint16_t)((((uint16_t)s_packet[0]) & 0x07U) << 8) | s_packet[1];
    if (packet_length != (uint16_t)packet_size) {
        ESP_LOGW(TAG, "packet size mismatch: recv=%d header=%u", (int)packet_size, (unsigned)packet_length);
        return;
    }

    uint8_t header_mask = (uint8_t)(s_packet[0] >> 3);
    s_session_id = atem_read_u16_be(&s_packet[2]);
    uint16_t acked_packet_id = atem_read_u16_be(&s_packet[4]);
    s_last_remote_packet_id = atem_read_u16_be(&s_packet[10]);

#if ATEM_DEBUG_PACKET_HEADERS
    if (s_debug_packet_logs < ATEM_DEBUG_MAX_PACKET_LOGS) {
        ESP_LOGI(
            TAG,
            "rx hdr mask=0x%02X len=%u session=0x%04X acked=%u remote_id=%u bytes=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
            (unsigned)header_mask,
            (unsigned)packet_length,
            (unsigned)s_session_id,
            (unsigned)acked_packet_id,
            (unsigned)s_last_remote_packet_id,
            (unsigned)s_packet[0],
            (unsigned)s_packet[1],
            (unsigned)s_packet[2],
            (unsigned)s_packet[3],
            (unsigned)s_packet[4],
            (unsigned)s_packet[5],
            (unsigned)s_packet[6],
            (unsigned)s_packet[7],
            (unsigned)s_packet[8],
            (unsigned)s_packet[9],
            (unsigned)s_packet[10],
            (unsigned)s_packet[11]
        );
        s_debug_packet_logs++;
    }
#endif

    if (s_last_remote_packet_id < ATEM_INIT_PACKET_COUNT) {
        s_missing_init_packets[s_last_remote_packet_id] = false;
    }

    s_last_contact_ms = atem_now_ms();
    atem_status_inc_packets_rx();

    if ((header_mask & ATEM_HELLO_PACKET) != 0U) {
        if (!s_connected) {
            ESP_LOGI(TAG, "Connection to ATEM switcher established, session=0x%04X", (unsigned)s_session_id);
        }
        s_connected = true;
        atem_send_first_hello_ack();
        atem_status_set_connected(true, s_initialized);
    }

    if (!s_init_payload_sent && packet_length == ATEM_HEADER_LEN && s_last_remote_packet_id > 1U) {
        s_init_payload_sent = true;
        s_init_payload_sent_at_packet_id = (uint8_t)s_last_remote_packet_id;
        ESP_LOGI(
            TAG,
            "initial payload end at remote packet %u, session=0x%04X",
            (unsigned)s_init_payload_sent_at_packet_id,
            (unsigned)s_session_id
        );
    }

    // ACK posíláme na každý packet, který si o něj řekne. Pro první živý test je to
    // bezpečnější než čekat na kompletní init sekvenci, která se u různých ATEMů může lišit.
    if ((header_mask & ATEM_ACK_REQUEST) != 0U) {
        atem_send_ack(s_last_remote_packet_id);
    }

    if (packet_length > ATEM_HEADER_LEN) {
        // Connect/hello odpověď má často délku 20 a není to seznam ATEM commandů.
        // Všechny ostatní payload pakety zkusíme parsovat i tehdy, když mají v hlavičce
        // nastavený HELLO bit. Některé ATEMy posílají část inicializace právě takhle.
        if (!(((header_mask & ATEM_HELLO_PACKET) != 0U) && packet_length == 20U)) {
            atem_debug_dump_packet_payload(packet_length);
            atem_parse_payload(packet_length);
#if ATEM_DEBUG_SCAN_KNOWN_COMMANDS
            atem_scan_for_known_commands(packet_length);
#endif
        }
    }

    if (!s_initialized && s_init_payload_sent) {
        s_initialized = true;
        ESP_LOGI(TAG, "ATEM data stream active");
        atem_status_set_connected(s_connected, true);
    }

    if (s_waiting_for_resend) {
        s_waiting_for_resend = false;
    }
}

static esp_err_t atem_socket_open(void)
{
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "socket create failed: errno=%d", errno);
        return ESP_FAIL;
    }

    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = ATEM_SOCKET_TIMEOUT_MS * 1000,
    };
    (void)setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in local_addr = {0};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(ATEM_CONTROL_LOCAL_PORT);

    if (bind(s_sock, (const struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        ESP_LOGE(TAG, "bind UDP port %d failed: errno=%d", ATEM_CONTROL_LOCAL_PORT, errno);
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    net_config_get_atem_ip_string(s_atem_ip_string, sizeof(s_atem_ip_string));

    memset(&s_atem_addr, 0, sizeof(s_atem_addr));
    s_atem_addr.sin_family = AF_INET;
    s_atem_addr.sin_port = htons(ATEM_CONTROL_SWITCHER_PORT);
    s_atem_addr.sin_addr.s_addr = inet_addr(s_atem_ip_string);

    if (s_atem_addr.sin_addr.s_addr == INADDR_NONE) {
        ESP_LOGE(TAG, "invalid ATEM IP: %s", s_atem_ip_string);
        close(s_sock);
        s_sock = -1;
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static void atem_task(void *arg)
{
    (void)arg;

    portENTER_CRITICAL(&s_status_mux);
    s_status.task_running = true;
    portEXIT_CRITICAL(&s_status_mux);

    if (atem_socket_open() != ESP_OK) {
        atem_status_set_connected(false, false);
        vTaskDelete(NULL);
        return;
    }

    atem_send_connect_packet();

    while (1) {
        ssize_t len = recvfrom(s_sock, s_packet, sizeof(s_packet), 0, NULL, NULL);
        if (len > 0) {
            atem_process_packet(len);
        }

        uint64_t now_ms = atem_now_ms();

        if (s_connected && (now_ms - s_last_contact_ms) > ATEM_TIMEOUT_MS) {
            ESP_LOGW(TAG, "Connection to ATEM timed out - reconnecting");
            s_connected = false;
            s_initialized = false;
            s_program_baseline_valid = false;
            atem_status_set_connected(false, false);
        }

        if (!s_connected && (now_ms - s_last_connect_try_ms) > ATEM_CONNECT_RETRY_MS) {
            atem_send_connect_packet();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t atem_control_init(void)
{
    if (s_task_handle != NULL) {
        return ESP_OK;
    }

    memset(&s_status, 0, sizeof(s_status));
    atem_status_set_connected(false, false);
    atem_status_set_program_preview(0, 0);

    BaseType_t ret = xTaskCreatePinnedToCore(
        atem_task,
        ATEM_TASK_NAME,
        ATEM_TASK_STACK_SIZE,
        NULL,
        ATEM_TASK_PRIORITY,
        &s_task_handle,
        ATEM_TASK_CORE
    );

    if (ret != pdPASS) {
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    net_config_get_atem_ip_string(s_atem_ip_string, sizeof(s_atem_ip_string));
    ESP_LOGI(TAG, "ATEM control task started on core %d, switcher IP %s", ATEM_TASK_CORE, s_atem_ip_string);
    return ESP_OK;
}

void atem_control_get_status(atem_control_status_t *status)
{
    if (!status) {
        return;
    }

    portENTER_CRITICAL(&s_status_mux);
    *status = s_status;
    portEXIT_CRITICAL(&s_status_mux);
}
