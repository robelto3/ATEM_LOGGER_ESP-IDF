idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES i2c_bus display rtc ltc app_state app_tasks fake_cut_button new_file_button tally_outputs cut_event logger_events logger_session edl_writer sd_storage net_config debug_control serial_console show_config file_protect net_eth web_server atem_control
)
