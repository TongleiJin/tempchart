idf_component_register(
    SRCS
        "main.cpp"
        "console_init.c"
        "console/cmd_board.c"
        "console/cmd_sensor.c"
        "ble/ble_app.c"
        "ble/gatt_svr.c"
    INCLUDE_DIRS
        "."
        "console"
        "ble"
    PRIV_REQUIRES
        app port_bsp console nvs_flash app_update esp_timer lvgl driver esp_adc
        bt esp_netif esp_event
)
