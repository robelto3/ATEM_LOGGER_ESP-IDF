#include <stdbool.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"

#include "app_state.h"
#include "app_tasks.h"
#include "atem_control.h"
#include "cut_event.h"
#include "debug_control.h"
#include "display.h"
#include "edl_writer.h"
#include "fake_cut_button.h"
#include "file_protect.h"
#include "i2c_bus.h"
#include "logger_events.h"
#include "logger_session.h"
#include "ltc.h"
#include "net_config.h"
#include "net_eth.h"
#include "new_file_button.h"
#include "rtc.h"
#include "sd_storage.h"
#include "serial_console.h"
#include "show_config.h"
#include "tally_outputs.h"
#include "web_server.h"

// =====================================================
// Main
// =====================================================

void app_main(void)
{
    // Výchozí běh má být tichý.
    // UART odpovídá pouze na ručně zadané příkazy.
    esp_log_level_set("*", ESP_LOG_NONE);

    ESP_ERROR_CHECK(i2c_bus_init());

    ESP_ERROR_CHECK(app_state_init());

    // Trvalé síťové nastavení z NVS + servisní UART konzole.
    // UART zatím umí jen změnu IP adresy ESP/web serveru.
    ESP_ERROR_CHECK(net_config_init());
    debug_control_init();
    ESP_ERROR_CHECK(serial_console_init());

    // Uložené názvy pořadů pro EDL TITLE.
    // Čítač pořadu se neukládá do NVS; dopočítává se z existujících EDL souborů na SD.
    ESP_ERROR_CHECK(show_config_init());

    // Ochrana vybraných EDL souborů proti smazání.
    // Stav ochrany je uložený v NVS.
    ESP_ERROR_CHECK(file_protect_init());

    // OLED není kritický pro běh loggeru, webu ani záznamu na SD.
    esp_err_t display_ret = display_init();
    if (display_ret == ESP_OK) {
        char startup_server_ip[NET_CONFIG_IP_STR_LEN];
        char startup_atem_ip[NET_CONFIG_IP_STR_LEN];
        net_config_get_server_ip_string(startup_server_ip, sizeof(startup_server_ip));
        net_config_get_atem_ip_string(startup_atem_ip, sizeof(startup_atem_ip));
        esp_err_t startup_ret = display_show_startup_screen(startup_server_ip, startup_atem_ip);
        if (startup_ret != ESP_OK && debug_control_is_enabled()) {
            printf("DISPLAY: startup screen FAILED: %s\n", esp_err_to_name(startup_ret));
        }
    } else if (debug_control_is_enabled()) {
        printf("DISPLAY: init FAILED: %s\n", esp_err_to_name(display_ret));
    }

    ESP_ERROR_CHECK(rtc_init());

    ESP_ERROR_CHECK(edl_writer_init());

    // Onboard SD slot pouze mountujeme.
    // Testovací /sdcard/test.txt se automaticky nevytváří.
    // Záměrně bez ESP_ERROR_CHECK, aby logger běžel dál i bez karty.
    (void)sd_storage_init();

    rtc_datetime_t rtc_boot = {0};
    esp_err_t rtc_boot_ret = rtc_read_datetime(&rtc_boot);
    app_state_update_rtc(&rtc_boot, rtc_boot_ret == ESP_OK);
    ESP_ERROR_CHECK(logger_session_init_from_rtc(&rtc_boot, rtc_boot_ret == ESP_OK));

    ESP_ERROR_CHECK(ltc_init());

    ESP_ERROR_CHECK(fake_cut_button_init());
    ESP_ERROR_CHECK(new_file_button_init());

    // Tally výstupy nejsou kritické pro běh loggeru.
    // Kdyby při přehazování pinů na budoucím PCB některý GPIO zlobil,
    // logger, SD, web i ATEM část poběží dál.
    esp_err_t tally_ret = tally_outputs_init();
    if (tally_ret != ESP_OK) {
        if (debug_control_is_enabled()) {
            printf("TALLY: init FAILED: %s\n", esp_err_to_name(tally_ret));
        }
    }

    ESP_ERROR_CHECK(cut_event_init());
    ESP_ERROR_CHECK(logger_events_init());

    // Aplikační tasky:
    // - rychlá taska na Core 1 drží LTC snapshot a obsluhuje tlačítka,
    // - pomalejší taska na Core 0 obnovuje OLED/RTC/debug.
    ESP_ERROR_CHECK(app_tasks_start());

    // Ethernet + web pro prohlížení SD souborů a nastavení IP.
    // Bez ESP_ERROR_CHECK, aby logger běžel dál i bez sítě/kabelu.
    esp_err_t eth_ret = net_eth_init_static();
    if (eth_ret == ESP_OK) {
        esp_err_t web_ret = web_server_start();
        if (web_ret != ESP_OK) {
            if (debug_control_is_enabled()) {
                printf("WEB: start FAILED: %s\n", esp_err_to_name(web_ret));
            }
        }

        // ATEM komponenta čte Program/Preview a změny Program busu posílá do logger fronty.
        esp_err_t atem_ret = atem_control_init();
        if (atem_ret != ESP_OK) {
            if (debug_control_is_enabled()) {
                printf("ATEM: init FAILED: %s\n", esp_err_to_name(atem_ret));
            }
        }
    } else {
        if (debug_control_is_enabled()) {
            printf("NET: init FAILED: %s - web and ATEM disabled\n", esp_err_to_name(eth_ret));
        }
        app_state_set_atem_connected(false);
        app_state_set_program_preview(0, 0);
    }
}
