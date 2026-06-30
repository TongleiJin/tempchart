idf_component_register(
    SRCS
        "main.cpp"
        "console_init.c"
        "console/cmd_board.c"
        "console/cmd_sensor.c"
    INCLUDE_DIRS
        "."
        "console"
    PRIV_REQUIRES
        app port_bsp console nvs_flash app_update esp_timer lvgl driver esp_adc
)
