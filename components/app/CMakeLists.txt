idf_component_register(
  SRCS
    "user_app.cpp"
    "lite_fifo.c"
    "temp_sampler.cpp"
    "chart_controller.c"
    "input_handler.cpp"
    "home_view.c"
    "device_status.c"
    "setting_actions_iface.c"
  INCLUDE_DIRS "."
  REQUIRES esp_timer firmware_version ota_url
  PRIV_REQUIRES
    port_bsp
    ui
    driver
    my_button
    codec_board
    waveshare__pcf85063a
    esp_wifi
    esp_netif
)
