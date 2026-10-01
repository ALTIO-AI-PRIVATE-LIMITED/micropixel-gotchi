#ifndef MICROPIXEL_FIRMWARE_REMOTE_BINDING_POLICY_HPP
#define MICROPIXEL_FIRMWARE_REMOTE_BINDING_POLICY_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "host/controller/remote/remote_control_protocol.hpp"

namespace micropixel::firmware::remote_control {

// Session-scoped control frames (device.binding) apply only to the control
// session that is currently open.
constexpr bool MatchesSessionFrame(double version, std::string_view frame_session_id, std::string_view session_id) {
    return version == protocol::kVersion && !session_id.empty() && frame_session_id == session_id;
}

// Length of the next UTF-8 sequence at text[0], or 0 when it is malformed.
constexpr size_t Utf8SequenceLength(std::string_view text) {
    if (text.empty()) return 0U;
    const auto lead = static_cast<uint8_t>(text[0]);
    size_t length = 0U;
    uint32_t minimum = 0U;
    uint32_t codepoint = 0U;
    if (lead < 0x80U) return 1U;
    if ((lead & 0xE0U) == 0xC0U) {
        length = 2U;
        minimum = 0x80U;
        codepoint = lead & 0x1FU;
    } else if ((lead & 0xF0U) == 0xE0U) {
        length = 3U;
        minimum = 0x800U;
        codepoint = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U) {
        length = 4U;
        minimum = 0x10000U;
        codepoint = lead & 0x07U;
    } else {
        return 0U;
    }
    if (text.size() < length) return 0U;
    for (size_t index = 1U; index < length; ++index) {
        const auto next = static_cast<uint8_t>(text[index]);
        if ((next & 0xC0U) != 0x80U) return 0U;
        codepoint = (codepoint << 6U) | (next & 0x3FU);
    }
    const bool surrogate = codepoint >= 0xD800U && codepoint <= 0xDFFFU;
    return codepoint < minimum || codepoint > 0x10FFFFU || surrogate ? 0U : length;
}

// Copies an owner name for display into a NUL-terminated buffer: control
// characters are dropped and the text is cut on a character boundary so it
// fits. Malformed UTF-8 yields an empty name. Returns the copied byte count.
constexpr size_t CopyOwnerName(std::string_view source, char* destination, size_t capacity) {
    if (destination == nullptr || capacity == 0U) return 0U;
    size_t written = 0U;
    size_t offset = 0U;
    while (offset < source.size()) {
        const size_t length = Utf8SequenceLength(source.substr(offset));
        if (length == 0U) {
            written = 0U;
            break;
        }
        const auto lead = static_cast<uint8_t>(source[offset]);
        const bool control = length == 1U && (lead < 0x20U || lead == 0x7FU);
        if (!control) {
            if (written + length > capacity - 1U) break;
            for (size_t index = 0U; index < length; ++index) destination[written++] = source[offset + index];
        }
        offset += length;
    }
    destination[written] = '\0';
    return written;
}

}  // namespace micropixel::firmware::remote_control

#endif
