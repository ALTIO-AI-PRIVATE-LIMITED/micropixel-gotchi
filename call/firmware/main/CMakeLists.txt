idf_component_register(SRCS "main.c" "board.c" "display.c" "media.c" "call.c" "call_server.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_spi
                                     esp_lcd esp_http_client esp_timer mbedtls json)
