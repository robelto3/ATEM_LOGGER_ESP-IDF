idf_component_register(
    SRCS "app_tasks.c"
    INCLUDE_DIRS "include"
    REQUIRES app_state display rtc ltc fake_cut_button new_file_button logger_events debug_control tally_outputs freertos esp_common
)
