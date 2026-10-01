// SPDX-License-Identifier: Apache-2.0
#include "platform/platform.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>

#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "host/ui/lvgl/square_common/profiles/landscape_296.hpp"
#include "host/ui/lvgl/square_common/square_presentation.hpp"
#include "host/ui/lvgl/square_common/square_system_ui.hpp"
#include "host/ui/lvgl/square_common/square_ui_state.hpp"
#include "host/ui/lvgl/square_common/status_layer_transition.hpp"
#include "platform/adapters/graphics_adapter.hpp"
#include "platform/audio/audio_engine.hpp"
#include "platform/audio/es8311_i2s_audio_sink.hpp"
#include "platform/boards/cheeko-gotchi/board_config.hpp"
#include "platform/boards/cheeko-gotchi/board_hardware.hpp"
#include "platform/boards/cheeko-gotchi/display_hardware.hpp"
#include "platform/boards/cheeko-gotchi/sensor_peripheral.hpp"
#include "platform/boards/esp32-s3-common/display_shadow.hpp"
#include "platform/buses/i2c_executor.hpp"
#include "platform/controllers/brightness_curve.hpp"
#include "platform/input/esp_lcd_touch_input.hpp"
#include "platform/input/gpio_key_input.hpp"
#include "platform/lvgl/display/screen_capture.hpp"
#include "platform/lvgl/fonts/font_registry.hpp"
#include "platform/lvgl/guest_graphics_engine.hpp"
#include "platform/lvgl/guest_graphics_operations.hpp"
#include "platform/memory/ext_ram_bss.hpp"
#include "platform/memory/internal_ram.hpp"
#include "platform/transports/development_display_control.hpp"
#include "platform/transports/usb_serial_jtag_local_control.hpp"
#include "platform/wifi/native_wifi_radio.hpp"
#include "platform/wifi/wifi_manager.hpp"
#include "work/task_policy.hpp"

namespace micropixel::platform {
namespace {

constexpr char kTag[] = "cheeko_gotchi";
namespace board_detail = cheeko_gotchi;

namespace detail {

namespace ui_profile = host_ui::lvgl::square_common::profiles::landscape_296;

inline constexpr int32_t kWidth = board_detail::board::kDisplayWidth;
inline constexpr int32_t kHeight = board_detail::board::kDisplayHeight;
static_assert(kWidth == ui_profile::Layout::kWidth);
static_assert(kHeight == ui_profile::Layout::kHeight);
inline constexpr int kLvglTaskCore = task_policy::kSystemCore;
inline constexpr graphics::SurfacePixelFormat kGuestSurfaceFormat = graphics::SurfacePixelFormat::kRgb565;
inline constexpr int kStartupBrightnessPercent = 80;
// ES8311 output only. The ES7210 microphones share the I2S bus but MicroPixel
// has no capture contract, so they stay in reset.
inline constexpr uint32_t kAudioSampleRate = 16000U;

// Board-owned input control stays in internal RAM, including the polled touch.
struct CheekoGotchiInputState final {
    buses::I2cExecutor i2c_executor{};
    input::EspLcdTouchInput touch_input{kWidth, kHeight, device::kMaxTouchPoints};
};

// Large task-only state lives in PSRAM and references Board-owned input
// controls, following the other S3 boards.
struct CheekoGotchiState final {
    explicit CheekoGotchiState(CheekoGotchiInputState& input)
        : i2c_executor(input.i2c_executor), touch_input(input.touch_input) {}

    lv_display_t* display{};
    buses::I2cExecutor& i2c_executor;
    input::EspLcdTouchInput& touch_input;
    lvgl::FontRegistry fonts{};
    lvgl::GuestGraphicsEngine guest_graphics{kWidth, kHeight, fonts, kGuestSurfaceFormat};
    host_ui::lvgl::square_common::StaticStatusLayerTransition status_transition{};
    host_ui::lvgl::square_common::SquareSystemUiState ui{touch_input, guest_graphics, status_transition,
                                                         ui_profile::kSystemUiProfile};
    // The adapter byte-swaps its partial buffers for the panel, so screenshots
    // read this canonical RGB565 copy instead.
    esp32_s3_common::DisplayShadow display_shadow{kWidth, kHeight};
    transports::UsbSerialJtagLocalControl local_control{};
    transports::DevelopmentDisplayControl development_display{};
};

}  // namespace detail

// Kept out of line so its adapter configs do not join Initialize()'s frame.
[[gnu::noinline]] esp_err_t RegisterLvglDisplay(detail::CheekoGotchiState& state,
                                                board_detail::DisplayHardware& hardware) {
    if (hardware.Panel() == nullptr || hardware.PanelIo() == nullptr || state.display != nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!esp_lv_adapter_is_initialized()) {
        esp_lv_adapter_config_t adapter_config{};
        adapter_config.task_stack_size = 10240U;
        adapter_config.task_priority = ESP_LV_ADAPTER_DEFAULT_TASK_PRIORITY;
        adapter_config.task_core_id = detail::kLvglTaskCore;
        adapter_config.tick_period_ms = ESP_LV_ADAPTER_DEFAULT_TICK_PERIOD_MS;
#ifdef ESP_LV_ADAPTER_HAS_TICK_MODE
        adapter_config.tick_mode = ESP_LV_ADAPTER_TICK_MODE_PERIODIC;
#endif
        adapter_config.task_min_delay_ms = 1U;
        adapter_config.task_max_delay_ms = 1U;
        adapter_config.stack_in_psram = true;
        adapter_config.auto_sleep.enable = false;
        adapter_config.auto_sleep.mode = ESP_LV_ADAPTER_AUTO_SLEEP_MODE_DISABLED;
        adapter_config.auto_sleep.idle_timeout_ms = ESP_LV_ADAPTER_DEFAULT_AUTO_SLEEP_TIMEOUT_MS;
        ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&adapter_config), kTag, "initialize LVGL adapter failed");
    }

    esp_lv_adapter_display_config_t display_config{};
    display_config.panel = hardware.Panel();
    display_config.panel_io = hardware.PanelIo();
    display_config.profile.interface = ESP_LV_ADAPTER_PANEL_IF_OTHER;
    display_config.profile.rotation = ESP_LV_ADAPTER_ROTATE_0;
    display_config.profile.hor_res = detail::kWidth;
    display_config.profile.ver_res = detail::kHeight;
    display_config.profile.buffer_height = CONFIG_MICROPIXEL_LVGL_PARTIAL_BUFFER_HEIGHT;
    display_config.profile.use_psram = false;
    display_config.profile.enable_ppa_accel = false;
    display_config.profile.require_double_buffer = false;
    display_config.profile.mono_layout = ESP_LV_ADAPTER_MONO_LAYOUT_NONE;
    display_config.tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE;
    display_config.te_sync = {};
    display_config.te_sync.gpio_num = -1;
    state.display = esp_lv_adapter_register_display(&display_config);
    if (state.display == nullptr) {
        return ESP_FAIL;
    }
    return esp_lcd_panel_disp_on_off(hardware.Panel(), true);
}

// Brightness, volume and screen capture; the remaining optional roles stay
// null and the shared UI remains fully usable without them.
class CheekoGotchiPresentation final : public host_ui::lvgl::square_common::SquarePresentation,
                                       public host_ui::lvgl::square_common::ScreenCapture,
                                       public host_ui::lvgl::square_common::BrightnessControl,
                                       public host_ui::lvgl::square_common::VolumeControl {
   public:
    CheekoGotchiPresentation(detail::CheekoGotchiState& state, board_detail::BoardHardware& hardware)
        : state_(state), hardware_(hardware) {}

    void BindAudioEngine(audio::AudioEngine& audio) { audio_ = &audio; }

    [[nodiscard]] host_ui::lvgl::square_common::ScreenCapture* Capture() override { return this; }
    [[nodiscard]] host_ui::lvgl::square_common::BrightnessControl* Brightness() override { return this; }
    [[nodiscard]] host_ui::lvgl::square_common::VolumeControl* Volume() override { return this; }

    [[nodiscard]] std::expected<host_ui::ScreenCapture, host_ui::SystemUiError> CaptureScreenJpeg() override {
        if (state_.display == nullptr || state_.display_shadow.Pixels() == nullptr) {
            return std::unexpected(host_ui::SystemUiError::kUnavailable);
        }
        return lvgl::CaptureScreenJpeg(state_.display, static_cast<uint32_t>(detail::kWidth),
                                       static_cast<uint32_t>(detail::kHeight),
                                       {.pixels = state_.display_shadow.Pixels(),
                                        .stride = state_.display_shadow.Stride(),
                                        .format = lvgl::DisplayCapturePixelFormat::kRgb565,
                                        .ready = state_.display_shadow.Ready()});
    }

    void ApplyBrightness(uint8_t percent) override {
        const auto bounded = std::min<uint8_t>(percent, 100U);
        const esp_err_t status = hardware_.SetBrightness(controllers::VisibleBrightnessPercent(bounded));
        if (status != ESP_OK) {
            ESP_LOGW(kTag, "apply brightness failed: %s", esp_err_to_name(status));
        }
    }

    // Host master volume: the one place the saved volume setting applies.
    void ApplyVolume(uint8_t percent) override {
        if (audio_ != nullptr) {
            audio_->SetMasterVolumePercent(percent);
        }
    }

   private:
    detail::CheekoGotchiState& state_;
    board_detail::BoardHardware& hardware_;
    audio::AudioEngine* audio_{};
};

class CheekoGotchiBoard final : public Board {
   public:
    CheekoGotchiBoard()
        : state_(TaskState(input_state_)),
          graphics_context_{.engine = &state_.guest_graphics, .hooks = state_.ui.GraphicsHooks()},
          graphics_(lvgl::MakeGuestGraphicsOperations(graphics_context_)),
          presentation_(state_, hardware_),
          system_ui_(state_.ui, presentation_) {
        state_.guest_graphics.SetPresentationHooks(state_.ui.GuestFrameHooks());
    }

    [[nodiscard]] esp_err_t Initialize(BoardContext& context) override {
        ESP_RETURN_ON_FALSE(memory::IsInternalObject(*this), ESP_ERR_INVALID_STATE, kTag,
                            "Board control objects must reside in internal RAM");
        // Holds the power latch, so it runs before anything that can fail.
        ESP_RETURN_ON_ERROR(hardware_.Initialize(), kTag, "initialize board hardware failed");
        presentation_.BindAudioEngine(context.AudioEngine());
        ESP_LOGI(kTag, "initializing Cheeko Gotchi (296x240 JD9853)");

        ESP_RETURN_ON_ERROR(display_.InitializePanel(), kTag, "initialize JD9853 display failed");
        ESP_RETURN_ON_ERROR(RegisterLvglDisplay(state_, display_), kTag, "register LVGL display failed");
        ESP_RETURN_ON_ERROR(state_.display_shadow.Initialize(state_.display), kTag,
                            "initialize PSRAM displayed shadow failed");
        ESP_RETURN_ON_ERROR(state_.i2c_executor.Initialize(), kTag, "start shared I2C executor failed");
        // A missing touch controller leaves the board usable from USB and keys.
        const esp_err_t touch_status = display_.InitializeTouch(hardware_.I2cBus());
        if (touch_status == ESP_OK) {
            ESP_RETURN_ON_ERROR(state_.touch_input.Initialize(display_.Touch(), state_.i2c_executor), kTag,
                                "bind CST810 touch input failed");
        } else {
            ESP_LOGW(kTag, "touch unavailable for this boot: %s", esp_err_to_name(touch_status));
        }
        ESP_RETURN_ON_ERROR(state_.guest_graphics.Initialize(state_.display, nullptr), kTag,
                            "initialize RGB565 Guest graphics failed");

        if (esp_lv_adapter_lock(-1) != ESP_OK) {
            return ESP_FAIL;
        }
        const esp_err_t ui_status = state_.ui.InitializeLocked(state_.display);
        esp_lv_adapter_unlock();
        ESP_RETURN_ON_ERROR(ui_status, kTag, "initialize 296x240 Host UI failed");
        ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), kTag, "start LVGL adapter failed");
        if (touch_status == ESP_OK) {
            ESP_RETURN_ON_ERROR(state_.touch_input.Start(state_.display), kTag, "start polled CST810 input failed");
        }

        const esp_err_t sensor_status = sensors_.Initialize(hardware_.I2cBus(), state_.i2c_executor);
        if (sensor_status != ESP_OK) {
            ESP_LOGW(kTag, "accelerometer unavailable for this boot: %s", esp_err_to_name(sensor_status));
        }
        const esp_err_t audio_status = audio_output_.Configure(hardware_.I2cBus(), state_.i2c_executor);
        if (audio_status != ESP_OK) {
            ESP_LOGW(kTag, "audio unavailable for this boot: %s", esp_err_to_name(audio_status));
        }
        ESP_RETURN_ON_ERROR(boot_button_.Initialize(state_.ui.Input()), kTag, "initialize BOOT key failed");
        ESP_RETURN_ON_ERROR(volume_down_button_.Initialize(state_.ui.Input()), kTag, "initialize VOL- key failed");
        ESP_RETURN_ON_ERROR(volume_up_button_.Initialize(state_.ui.Input()), kTag, "initialize VOL+ key failed");
        // USB development screenshots take the same path as Remote Control.
        ESP_RETURN_ON_ERROR(
            state_.development_display.Start(state_.touch_input, state_.local_control, detail::kWidth, detail::kHeight,
                                             transports::DevelopmentCaptureHook::For(presentation_)),
            kTag, "start USB development control failed");
        ESP_RETURN_ON_ERROR(hardware_.SetBrightness(detail::kStartupBrightnessPercent), kTag,
                            "set startup brightness failed");

        // Every peripheral slot lives in this object; PSRAM keeps it off the stack.
        static MICROPIXEL_EXT_RAM_BSS BoardRegistration registration{{
            .board = "Cheeko Gotchi",
            .host_chip = "ESP32-S3",
            .firmware_target = "cheeko-gotchi",
            .wifi_coprocessor = "Native ESP32-S3",
            .touch_controller = touch_status == ESP_OK ? "CST810" : "Unavailable",
            .display =
                {
                    .driver = "JD9853",
                    .interface = "SPI 40 MHz",
                    .pixel_format = "RGB565 (big-endian wire)",
                    .width_pixels = detail::kWidth,
                    .height_pixels = detail::kHeight,
                },
            .graphics_acceleration = "CPU only; SPI DMA transport",
        }};
        registration.SetInput(state_.ui.Input());
        registration.SetGraphics(graphics_);
        if (audio_status == ESP_OK) {
            registration.SetAudioOutput(audio_output_, audio_output_.SampleRate());
        }
        registration.SetWifi(wifi_);
        registration.SetLocalControl(state_.local_control);
        registration.SetSystemUi(system_ui_);
        bool registered = true;
        if (sensors_.acceleration_available()) {
            registered = registration.AddSensor(sensors_, board_detail::SensorPeripheral::kAcceleration,
                                                "Built-in accelerometer") &&
                         registered;
        }
        ESP_LOGI(kTag, "ready: JD9853 + %s, native Wi-Fi, audio=%s, accel=%s",
                 touch_status == ESP_OK ? "CST810" : "no touch", audio_status == ESP_OK ? "ES8311" : "off",
                 sensors_.acceleration_available() ? "on" : "off");
        return registered && context.Publish(registration) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    void BindBackgroundExecutor(work::BackgroundExecutor& executor) override {
        state_.ui.BindBackgroundExecutor(executor);
        state_.guest_graphics.BindBackgroundExecutor(executor);
        wifi_.BindBackgroundExecutor(executor);
    }

   private:
    static detail::CheekoGotchiState& TaskState(detail::CheekoGotchiInputState& input) {
        static MICROPIXEL_EXT_RAM_BSS detail::CheekoGotchiState state(input);
        return state;
    }

    static input::GpioKeyInputConfig KeyConfig(gpio_num_t pin, device::KeyCode code, const char* name) {
        return {.pin = pin, .code = code, .log_tag = name, .task_name = name, .task_core = task_policy::kSystemCore};
    }

    detail::CheekoGotchiInputState input_state_{};
    detail::CheekoGotchiState& state_;
    lvgl::GuestGraphicsOperationsContext graphics_context_{};
    adapters::GraphicsAdapter graphics_;
    board_detail::BoardHardware hardware_{};
    board_detail::DisplayHardware display_{};
    board_detail::SensorPeripheral sensors_{};
    audio::Es8311I2sAudioSink audio_output_{{
                                                .name = "Cheeko Gotchi ES8311/I2S",
                                                .log_tag = "gotchi_audio",
                                                .i2c_port = board_detail::board::kI2cPort,
                                                .i2s_port = I2S_NUM_0,
                                                .master_clock = board_detail::board::kI2sMasterClock,
                                                .bit_clock = board_detail::board::kI2sBitClock,
                                                .word_select = board_detail::board::kI2sWordSelect,
                                                .data_out = board_detail::board::kI2sDataOut,
                                                .amplifier_enable = board_detail::board::kAmplifierEnable,
                                                .codec_i2c_address = 0x18U,
                                                .sample_rate = detail::kAudioSampleRate,
                                                .i2c_clock_hz = 100000U,
                                                .amplifier_preroll_ms = 24U,
                                                .dma_descriptor_count = 6U,
                                                .dma_frame_count = 240U,
                                                .amplifier_active_low = false,
                                                .probe_before_attach = true,
                                            },
                                            // The NS4150B runs from the battery rail, above the codec's 3.3 V.
                                            {.amplifier_voltage = 5.0F, .codec_dac_voltage = 3.3F}};
    CheekoGotchiPresentation presentation_;
    host_ui::lvgl::square_common::SquareSystemUi system_ui_;
    // Apps see the three front keys as confirm and left/right.
    input::GpioKeyInput boot_button_{
        KeyConfig(board_detail::board::kBootButton, device::KeyCode::kConfirm, "gotchi_boot")};
    input::GpioKeyInput volume_down_button_{
        KeyConfig(board_detail::board::kVolumeDownButton, device::KeyCode::kLeft, "gotchi_vol_down")};
    input::GpioKeyInput volume_up_button_{
        KeyConfig(board_detail::board::kVolumeUpButton, device::KeyCode::kRight, "gotchi_vol_up")};
    wifi::NativeWifiRadio wifi_radio_{};
    wifi::WifiManager wifi_{wifi_radio_};
};

}  // namespace

Board& ConfiguredBoard() {
    static CheekoGotchiBoard board;
    return board;
}

}  // namespace micropixel::platform
