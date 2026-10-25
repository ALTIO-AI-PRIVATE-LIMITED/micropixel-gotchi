#ifndef MICROPIXEL_PLATFORM_BLUETOOTH_BLE_HID_KEYBOARD_HPP
#define MICROPIXEL_PLATFORM_BLUETOOTH_BLE_HID_KEYBOARD_HPP

#include <array>
#include <atomic>
#include <cstdint>

#include "device/contracts/hid_keyboard.hpp"
#include "platform/bluetooth/ble_app_link.hpp"

namespace micropixel::platform::bluetooth {

// NimBLE HID-over-GATT keyboard. Start() brings up the controller, host and
// HID service once; the radio then stays up for the rest of the boot so the
// bonded computer keeps its connection while the user moves between Apps.
// Bonds persist in the default `nvs` partition.
//
// Report 1 is a boot style keyboard (modifiers, reserved, six keys) and
// report 2 a one-bit Apple vendor Fn usage, as on the WisprKey ESP32-C3
// button; report 3 is one Consumer page usage (volume, media); report 4 is a
// relative mouse (three buttons, X, Y, wheel, AC Pan), which an iPhone with
// AssistiveTouch shows as a pointer. VID/PID are the WisprKey's, published
// through the DIS PnP ID, so macOS remappers that match the original button
// keep working. An attached BleAppLink is served on the same host and
// connection.
class BleHidKeyboard final : public device::HidKeyboard {
   public:
    explicit BleHidKeyboard(const char* device_name) : device_name_(device_name) {}

    // Before Start(): its GATT service is registered with the keyboard's.
    void AttachLink(BleAppLink& link) { link_ = &link; }

    [[nodiscard]] uint32_t StateFlags() const override;
    [[nodiscard]] int32_t Start() override;
    [[nodiscard]] int32_t Key(uint8_t usage, bool pressed, uint32_t flags) override;
    [[nodiscard]] int32_t Consumer(uint16_t usage, bool pressed) override;
    [[nodiscard]] int32_t Mouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel, int8_t pan) override;
    [[nodiscard]] int32_t ForgetBonds() override;

    // NimBLE and esp_hidd callbacks; not part of the device contract.
    void OnHidStarted();
    void OnConnected(void* device);
    void OnDisconnected();
    void OnAdvertisingStopped();
    void OnLink(uint16_t connection);

   private:
    void Advertise();

    const char* device_name_{};
    BleAppLink* link_{};
    void* hid_device_{};
    std::atomic<uint16_t> connection_{0xFFFFU};
    // Keys currently held, in report order; written only by the Guest task.
    std::array<uint8_t, 6U> keys_{};
    uint8_t modifiers_{};
    std::atomic<uint32_t> flags_{};
    std::atomic<bool> starting_{};
};

}  // namespace micropixel::platform::bluetooth

#endif
