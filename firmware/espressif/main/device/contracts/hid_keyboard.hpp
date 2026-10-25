#ifndef MICROPIXEL_DEVICE_HID_KEYBOARD_HPP
#define MICROPIXEL_DEVICE_HID_KEYBOARD_HPP

#include <cstdint>

#include "abi/micropixel_abi.h"

namespace micropixel::device {

// Host-owned Bluetooth LE keyboard. One instance per board; the radio stays
// up once started so a bonded computer keeps its connection across Apps.
class HidKeyboard {
   public:
    virtual ~HidKeyboard() = default;
    HidKeyboard(const HidKeyboard&) = delete;
    HidKeyboard& operator=(const HidKeyboard&) = delete;

    // MICROPIXEL_HID_KEYBOARD_STATE_* flags.
    [[nodiscard]] virtual uint32_t StateFlags() const = 0;
    [[nodiscard]] virtual int32_t Start() = 0;
    // `usage` is a HID Keyboard/Keypad page usage (0xE0..0xE7 are the
    // modifiers); `flags` carries MICROPIXEL_HID_KEYBOARD_KEY_* bits.
    [[nodiscard]] virtual int32_t Key(uint8_t usage, bool pressed, uint32_t flags) = 0;
    // `usage` is a HID Consumer page usage such as Volume Increment (0xE9).
    [[nodiscard]] virtual int32_t Consumer(uint16_t usage, bool pressed) = 0;
    // One relative mouse report; `buttons` are MICROPIXEL_HID_KEYBOARD_MOUSE_* bits.
    [[nodiscard]] virtual int32_t Mouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel, int8_t pan) {
        (void)buttons;
        (void)dx;
        (void)dy;
        (void)wheel;
        (void)pan;
        return MICROPIXEL_STATUS_UNSUPPORTED;
    }
    // Erases every bond, drops the link and advertises again.
    [[nodiscard]] virtual int32_t ForgetBonds() { return MICROPIXEL_STATUS_UNSUPPORTED; }

   protected:
    HidKeyboard() = default;
};

}  // namespace micropixel::device

#endif
