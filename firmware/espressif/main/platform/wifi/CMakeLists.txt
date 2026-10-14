list(APPEND micropixel_sources
    "platform/wifi/wifi_manager.cpp"
)
# ESP-NOW needs the native Wi-Fi driver, not an esp-hosted coprocessor.
if(CONFIG_MICROPIXEL_BOARD_CHEEKO_GOTCHI)
    list(APPEND micropixel_sources "platform/wifi/esp_now_peer_radio.cpp")
endif()
