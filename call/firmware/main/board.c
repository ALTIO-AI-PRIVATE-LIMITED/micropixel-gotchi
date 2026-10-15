// CGotchi (Cheeko Gotchi v1.2) bring-up for the call firmware: power latch,
// buttons, and the ES8311 speaker codec + ES7210 microphone ADC.
//
// Adapted from LiveKit's custom_hardware example. Pins come from
// platform/boards/cheeko-gotchi/board_config.hpp in the MicroPixel port.

#include "board.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

// Soft power latch: HIGH cuts the battery rail, so it is driven LOW first.
#define BOARD_POWER_LATCH GPIO_NUM_2

#define BOARD_I2C_SCL  GPIO_NUM_11
#define BOARD_I2C_SDA  GPIO_NUM_12

// One full-duplex I2S port shared by ES8311 (TX) and ES7210 (RX).
#define BOARD_I2S_MCLK GPIO_NUM_5
#define BOARD_I2S_BCLK GPIO_NUM_15
#define BOARD_I2S_WS   GPIO_NUM_16
#define BOARD_I2S_DOUT GPIO_NUM_6
#define BOARD_I2S_DIN  GPIO_NUM_7

// NS4150B class-D amp enable, active high.
#define BOARD_PA_PIN   GPIO_NUM_4

// ES8311 at 7-bit 0x18 and ES7210 at 0x40: the esp_codec_dev defaults
// (which take the 8-bit form).
#define ES8311_ADDR    ES8311_CODEC_DEFAULT_ADDR
#define ES7210_ADDR    ES7210_CODEC_DEFAULT_ADDR

static i2c_master_bus_handle_t i2c_bus;
static i2s_chan_handle_t       i2s_tx;
static i2s_chan_handle_t       i2s_rx;
static esp_codec_dev_handle_t  play_dev;
static esp_codec_dev_handle_t  rec_dev;

static void init_power_latch(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_POWER_LATCH,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_set_level(BOARD_POWER_LATCH, 0);
    gpio_config(&cfg);
    gpio_set_level(BOARD_POWER_LATCH, 0);
}

static void init_buttons(void)
{
    // Vol- and Vol+ are active low and need the internal pull-up.
    gpio_config_t vol = {
        .pin_bit_mask = (1ULL << BOARD_BUTTON_VOL_DOWN) | (1ULL << BOARD_BUTTON_VOL_UP),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&vol);
    // The middle key is active high with an external pull-down: no pull-up.
    gpio_config_t key = {
        .pin_bit_mask = 1ULL << BOARD_BUTTON_MIDDLE,
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&key);
}

static esp_err_t init_i2c(void)
{
    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port   = I2C_NUM_0,
        .scl_io_num = BOARD_I2C_SCL,
        .sda_io_num = BOARD_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &i2c_bus);
}

static esp_err_t init_i2s(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &i2s_tx, &i2s_rx), TAG, "I2S channel");

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK,
            .bclk = BOARD_I2S_BCLK,
            .ws   = BOARD_I2S_WS,
            .dout = BOARD_I2S_DOUT,
            .din  = BOARD_I2S_DIN,
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(i2s_tx, &std_cfg), TAG, "I2S TX");

    // ES7210 in four-slot TDM, as the Gotchi factory firmware reads it.
    // Slot 0 = MIC1, slot 1 = MIC2, slot 2 = speaker loopback (AEC reference).
    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg  = I2S_TDM_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO,
            I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK,
            .bclk = BOARD_I2S_BCLK,
            .ws   = BOARD_I2S_WS,
            .dout = BOARD_I2S_DOUT,
            .din  = BOARD_I2S_DIN,
        },
    };
    tdm_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    tdm_cfg.slot_cfg.total_slot = 4;
    ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(i2s_rx, &tdm_cfg), TAG, "I2S RX");

    // The codecs need the clocks running while their registers are written.
    i2s_channel_enable(i2s_tx);
    i2s_channel_enable(i2s_rx);
    return ESP_OK;
}

static esp_err_t init_es8311(void)
{
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port       = I2C_NUM_0,
        .bus_handle = i2c_bus,
        .addr       = ES8311_ADDR,
    };
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(ctrl, ESP_FAIL, TAG, "ES8311 I2C ctrl");

    const audio_codec_gpio_if_t *gpio = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(gpio, ESP_FAIL, TAG, "GPIO if");

    es8311_codec_cfg_t codec_cfg = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .ctrl_if    = ctrl,
        .gpio_if    = gpio,
        .pa_pin     = BOARD_PA_PIN,
        .use_mclk   = true,
        .hw_gain    = { .pa_gain = 6.0 },
    };
    const audio_codec_if_t *codec = es8311_codec_new(&codec_cfg);
    ESP_RETURN_ON_FALSE(codec, ESP_FAIL, TAG, "ES8311 codec");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port      = I2S_NUM_0,
        .tx_handle = i2s_tx,
        .rx_handle = i2s_rx,
    };
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(data, ESP_FAIL, TAG, "I2S data if (out)");

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = codec,
        .data_if  = data,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    play_dev = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(play_dev, ESP_FAIL, TAG, "playback device");
    return ESP_OK;
}

static esp_err_t init_es7210(void)
{
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port       = I2C_NUM_0,
        .bus_handle = i2c_bus,
        .addr       = ES7210_ADDR,
    };
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(ctrl, ESP_FAIL, TAG, "ES7210 I2C ctrl");

    // All four inputs selected puts the ES7210 in TDM, like the factory firmware.
    es7210_codec_cfg_t codec_cfg = {
        .ctrl_if      = ctrl,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
    };
    const audio_codec_if_t *codec = es7210_codec_new(&codec_cfg);
    ESP_RETURN_ON_FALSE(codec, ESP_FAIL, TAG, "ES7210 codec");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port      = I2S_NUM_0,
        .rx_handle = i2s_rx,
    };
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(data, ESP_FAIL, TAG, "I2S data if (in)");

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = codec,
        .data_if  = data,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    rec_dev = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(rec_dev, ESP_FAIL, TAG, "record device");

    esp_codec_dev_set_in_gain(rec_dev, CONFIG_GOTCHI_CALL_MIC_GAIN_DB);
    return ESP_OK;
}

void board_init(void)
{
    init_power_latch();
    init_buttons();
    ESP_ERROR_CHECK(init_i2c());
    ESP_ERROR_CHECK(init_i2s());
    ESP_ERROR_CHECK(init_es8311());
    ESP_ERROR_CHECK(init_es7210());
    ESP_LOGI(TAG, "CGotchi audio ready: ES8311 speaker, ES7210 mics");
}

void board_power_off(void)
{
    // On battery this cuts the rail; on USB the board just keeps running.
    gpio_set_level(BOARD_POWER_LATCH, 1);
}

esp_codec_dev_handle_t board_get_playback_handle(void)
{
    return play_dev;
}

esp_codec_dev_handle_t board_get_record_handle(void)
{
    return rec_dev;
}
