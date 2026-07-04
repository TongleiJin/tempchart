idf_component_register(
  SRCS
    "user_app.cpp"
    "lite_fifo.c"
    "temp_sampler.cpp"
    "chart_controller.c"
    "input_handler.cpp"
    "home_view.c"
  INCLUDE_DIRS "."
  REQUIRES esp_timer
  PRIV_REQUIRES
    port_bsp
    ui
    driver
    my_button
    codec_board
    waveshare__pcf85063a
)
