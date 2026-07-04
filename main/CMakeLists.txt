idf_build_get_property(project_dir PROJECT_DIR)

idf_component_register(
    SRCS
        "main.cpp"
        "console_init.c"
        "console/cmd_board.c"
        "console/cmd_sensor.c"
        "console/cmd_wifi_conn.c"
        "console/cmd_ping.c"
        "console/cmd_ntp.c"
        "console/cmd_ota.c"
        "ble/ble_app.c"
        "ble/gatt_svr.c"
    INCLUDE_DIRS
        "."
        "console"
        "ble"
    PRIV_REQUIRES
        app port_bsp console nvs_flash app_update esp_timer lvgl driver esp_adc
        bt esp_netif esp_event esp_http_client esp_wifi lwip
    EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem
)
