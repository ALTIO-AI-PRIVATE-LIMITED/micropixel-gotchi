#ifndef MICROPIXEL_SDK_HID_KEYBOARD_HPP
#define MICROPIXEL_SDK_HID_KEYBOARD_HPP

#include <stdint.h>

#include "sdk/result.hpp"

namespace micropixel {

class Application;

// HID Keyboard/Keypad page usages accepted by the Host keyboard. Letters are
// kA + offset; HidLetter() maps 'a'..'z'.
enum class HidKeyUsage : uint8_t {
    kA = 0x04,
    kEnter = 0x28,
    kEscape = 0x29,
    kSpace = 0x2C,
    kF24 = 0x73,
    kLeftControl = 0xE0,
    kLeftShift = 0xE1,
    kLeftAlt = 0xE2,
    kLeftGui = 0xE3,
};

[[nodiscard]] constexpr HidKeyUsage HidLetter(char lowercase) {
    return static_cast<HidKeyUsage>(static_cast<uint8_t>(HidKeyUsage::kA) + static_cast<uint8_t>(lowercase - 'a'));
}

// HID Consumer page usages (media keys).
enum class HidConsumerUsage : uint16_t {
    kMute = 0xE2,
    kVolumeUp = 0xE9,
    kVolumeDown = 0xEA,
};

struct HidKeyboardState final {
    bool available{};
    bool started{};
    bool advertising{};
    bool connected{};
};

// Bits of Mouse() `buttons`.
enum class HidMouseButton : uint8_t {
    kLeft = 1U << 0U,
    kRight = 1U << 1U,
    kMiddle = 1U << 2U,
};

// Bluetooth LE keyboard owned by the Host. Start() powers the radio and makes
// the device pairable; once paired, the computer reconnects by itself and the
// link outlives the App. Key() fails with kInvalidState while nothing is
// connected. The same device is also a relative mouse (an iPhone with
// AssistiveTouch shows it as a pointer).
class HidKeyboard final {
   public:
    [[nodiscard]] Result<HidKeyboardState> State() const;
    [[nodiscard]] Result<void> Start() const;
    // `apple_fn` also drives the Apple vendor Fn bit with this key.
    [[nodiscard]] Result<void> Key(HidKeyUsage usage, bool pressed, bool apple_fn = false) const;
    [[nodiscard]] Result<void> Consumer(HidConsumerUsage usage, bool pressed) const;
    // One report: the buttons held after it (HidMouseButton bits) and relative motion.
    [[nodiscard]] Result<void> Mouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel = 0, int8_t pan = 0) const;
    // Erases every bond, drops the link and advertises again.
    [[nodiscard]] Result<void> ForgetBonds() const;

   private:
    struct CapabilityToken final {
       private:
        constexpr CapabilityToken() = default;
        friend class Application;
    };
    explicit constexpr HidKeyboard(CapabilityToken) noexcept {}
    friend class Application;
};

}  // namespace micropixel

#endif
