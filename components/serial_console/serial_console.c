#include "serial_console.h"

#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net_config.h"
#include "debug_control.h"

#define SERIAL_CONSOLE_TASK_NAME       "serial_console"
#define SERIAL_CONSOLE_TASK_STACK_SIZE 3072
#define SERIAL_CONSOLE_TASK_PRIORITY   2
#define SERIAL_CONSOLE_TASK_CORE       0
#define SERIAL_CONSOLE_LINE_MAX        64

// Výchozí UART režim je tichý:
// - žádná úvodní hláška,
// - žádný trvalý prompt,
// - žádné echo znaků.
// Odpověď se vypíše pouze po zadání příkazu.
#define SERIAL_CONSOLE_ECHO_INPUT      0
#define SERIAL_CONSOLE_PRINT_PROMPT    0

static TaskHandle_t s_task_handle = NULL;

static char *serial_console_trim(char *s)
{
    if (!s) {
        return s;
    }

    while (*s && isspace((unsigned char)*s)) {
        s++;
    }

    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';

    return s;
}

static void serial_console_print_help(void)
{
    printf("\n");
    printf("ATEM Logger serial console\n");
    printf("Commands:\n");
    printf("  help                 show this help\n");
    printf("  ip                   show saved ESP/server IP\n");
    printf("  ip 192.168.1.250     save new ESP/server IP\n");
    printf("  debug                enable debug prints\n");
    printf("  debug on             enable debug prints\n");
    printf("  debug off            disable debug prints\n");
    printf("  debug status         show debug state\n");
    printf("  reboot               restart ESP\n");
    printf("\n");
    printf("Note: IP change is applied after reboot.\n");
    printf("ATEM IP is changed only on the web page: /network\n");
}


static void serial_console_print_debug_status(void)
{
    printf("Debug prints: %s\n", debug_control_is_enabled() ? "ON" : "OFF");
}

static void serial_console_print_ip(void)
{
    char server_ip[NET_CONFIG_IP_STR_LEN] = {0};
    char netmask[NET_CONFIG_IP_STR_LEN] = {0};
    char atem_ip[NET_CONFIG_IP_STR_LEN] = {0};

    net_config_get_server_ip_string(server_ip, sizeof(server_ip));
    net_config_get_atem_ip_string(atem_ip, sizeof(atem_ip));
    net_config_get_netmask_string(netmask, sizeof(netmask));

    printf("ESP/server IP: %s\n", server_ip);
    printf("ATEM IP:       %s  (change on web /network)\n", atem_ip);
    printf("Netmask:       %s\n", netmask);
}

static void serial_console_process_line(char *line)
{
    char *cmd = serial_console_trim(line);

    if (cmd[0] == '\0') {
        return;
    }

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        serial_console_print_help();
        return;
    }

    if (strcmp(cmd, "ip") == 0) {
        serial_console_print_ip();
        return;
    }

    if (strcmp(cmd, "debug") == 0 || strcmp(cmd, "debug on") == 0) {
        debug_control_set_enabled(true);
        printf("Debug prints: ON\n");
        return;
    }

    if (strcmp(cmd, "debug off") == 0) {
        debug_control_set_enabled(false);
        printf("Debug prints: OFF\n");
        return;
    }

    if (strcmp(cmd, "debug status") == 0) {
        serial_console_print_debug_status();
        return;
    }

    if (strncmp(cmd, "ip ", 3) == 0) {
        char *ip_text = serial_console_trim(cmd + 3);

        esp_err_t ret = net_config_set_server_ip_string(ip_text);
        if (ret == ESP_OK) {
            char server_ip[NET_CONFIG_IP_STR_LEN] = {0};
            net_config_get_server_ip_string(server_ip, sizeof(server_ip));

            printf("Server IP saved: %s\n", server_ip);
            printf("Restart ESP to apply, or type: reboot\n");
        } else if (ret == ESP_ERR_INVALID_ARG) {
            printf("Invalid IP address. Example: ip 192.168.1.250\n");
            printf("Avoid .0 and .255 addresses.\n");
        } else {
            printf("Saving IP failed: %s\n", esp_err_to_name(ret));
        }
        return;
    }

    if (strcmp(cmd, "reboot") == 0 || strcmp(cmd, "restart") == 0) {
        printf("Rebooting...\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
        return;
    }

    printf("Unknown command: %s\n", cmd);
    printf("Type: help\n");
}

static void serial_console_task(void *arg)
{
    (void)arg;

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    char line[SERIAL_CONSOLE_LINE_MAX] = {0};
    size_t pos = 0;
    bool ignore_next_lf = false;

    // Tichý start: žádná úvodní hláška ani prompt.

    while (1) {
        int ch = getchar();

        if (ch == EOF) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (ch == '\n' && ignore_next_lf) {
            ignore_next_lf = false;
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            ignore_next_lf = (ch == '\r');

            if (pos > 0U) {
                line[pos] = '\0';
                printf("\n");
                serial_console_process_line(line);
                pos = 0;
                line[0] = '\0';
#if SERIAL_CONSOLE_PRINT_PROMPT
                printf("> ");
                fflush(stdout);
#endif
            }
            continue;
        }

        if (ch == '\b' || ch == 0x7F) {
            if (pos > 0U) {
                pos--;
                line[pos] = '\0';
#if SERIAL_CONSOLE_ECHO_INPUT
                printf("\b \b");
                fflush(stdout);
#endif
            }
            continue;
        }

        if (!isprint((unsigned char)ch)) {
            continue;
        }

        if (pos + 1U < sizeof(line)) {
            line[pos++] = (char)ch;
#if SERIAL_CONSOLE_ECHO_INPUT
            putchar(ch); // vlastní echo do monitoru
            fflush(stdout);
#endif
        } else {
            printf("\nLine too long. Type: help\n");
            pos = 0;
            line[0] = '\0';
#if SERIAL_CONSOLE_PRINT_PROMPT
            printf("> ");
            fflush(stdout);
#endif
        }
    }
}

esp_err_t serial_console_init(void)
{
    if (s_task_handle != NULL) {
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        serial_console_task,
        SERIAL_CONSOLE_TASK_NAME,
        SERIAL_CONSOLE_TASK_STACK_SIZE,
        NULL,
        SERIAL_CONSOLE_TASK_PRIORITY,
        &s_task_handle,
        SERIAL_CONSOLE_TASK_CORE
    );

    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}
