# Board, display and audio pipelines are shared with Gotchi Call (call/firmware).
set(CALL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../call/firmware/main")
idf_component_register(SRCS "main.c" "meet.c" "meet_server.c"
                            "${CALL_DIR}/board.c" "${CALL_DIR}/display.c" "${CALL_DIR}/media.c"
                       INCLUDE_DIRS "." "${CALL_DIR}"
                       PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_spi
                                     esp_lcd esp_http_client esp_timer mbedtls json)
