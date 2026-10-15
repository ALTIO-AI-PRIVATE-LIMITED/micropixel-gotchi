#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "livekit.h"
#include "livekit_example_utils.h"

#include "board.h"
#include "call.h"
#include "display.h"
#include "media.h"

void app_main(void)
{
    livekit_system_init();
    board_init();
    display_init();

    display_text_centered(98, "Wi-Fi...", DISPLAY_FONT_LARGE, COLOR_WHITE);
    display_flush();

    if (media_init() != 0) {
        display_fill(COLOR_BLACK);
        display_text_centered(98, "Audio error", DISPLAY_FONT_LARGE, COLOR_RED);
        display_flush();
        return;
    }

    // LiveKit tokens are time-checked over TLS, so the clock must be set.
    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        2, ESP_SNTP_SERVER_LIST("time.google.com", "pool.ntp.org"));
    esp_netif_sntp_init(&sntp_config);

    if (!lk_example_network_connect()) {
        display_fill(COLOR_BLACK);
        display_text_centered(98, "No Wi-Fi", DISPLAY_FONT_LARGE, COLOR_RED);
        display_flush();
        return;
    }
    call_start();
}
