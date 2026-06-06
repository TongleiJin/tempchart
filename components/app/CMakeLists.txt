idf_component_register(
  SRCS "user_app.cpp" "lite_fifo.c"
  PRIV_REQUIRES
  port_bsp
  ui
  driver
  my_button
  codec_board
  waveshare__pcf85063a
  REQUIRES
  PRIV_REQUIRES esp_timer
  INCLUDE_DIRS ".")
