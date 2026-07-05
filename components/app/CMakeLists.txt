idf_component_register(
  SRCS
    "user_app.cpp"
    "lite_fifo.c"
<<<<<<< HEAD
    "temp_sampler.cpp"
    "chart_controller.c"
    "input_handler.cpp"
    "home_view.c"
  INCLUDE_DIRS "."
  REQUIRES esp_timer
  PRIV_REQUIRES
=======
    "wifi_status.c"
   
    "app_version.c"
  REQUIRES
>>>>>>> d1bbfd2 (display about on home 2nd page)
    port_bsp
    ui
    driver
    my_button
    codec_board
    waveshare__pcf85063a
<<<<<<< HEAD
=======
    nvs_flash
    esp_netif
    esp_wifi
    app_update
    esp_timer
  INCLUDE_DIRS "."
>>>>>>> d1bbfd2 (display about on home 2nd page)
)
