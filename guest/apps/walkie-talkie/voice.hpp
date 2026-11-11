// SPDX-License-Identifier: Apache-2.0
//
// Voice path helpers: G.711 mu-law (8 bits per sample, ~38 dB SNR; the
// telephone standard and far cleaner than 4-bit ADPCM), and the talker-side
// cleanup that runs before encoding: a high-pass filter for DC and rumble and
// a noise gate that keeps the mic's own hiss out of the pauses.
#pragma once

#include <stdint.h>

namespace walkie {

inline uint8_t MuLawEncode(int16_t sample) {
    constexpr int32_t kBias = 0x84;
    constexpr int32_t kClip = 32635;
    int32_t value = sample;
    const uint8_t sign = value < 0 ? 0x80U : 0x00U;
    if (value < 0) {
        value = -value;
    }
    value = (value > kClip ? kClip : value) + kBias;
    uint8_t exponent = 7U;
    for (int32_t mask = 0x4000; (value & mask) == 0 && exponent > 0U; mask >>= 1) {
        --exponent;
    }
    const uint8_t mantissa = static_cast<uint8_t>((value >> (exponent + 3U)) & 0x0F);
    return static_cast<uint8_t>(~(sign | (exponent << 4U) | mantissa));
}

inline int16_t MuLawDecode(uint8_t code) {
    code = static_cast<uint8_t>(~code);
    const int32_t exponent = (code >> 4U) & 0x07;
    const int32_t mantissa = code & 0x0F;
    const int32_t magnitude = (((mantissa << 3) + 0x84) << exponent) - 0x84;
    return static_cast<int16_t>((code & 0x80U) != 0U ? -magnitude : magnitude);
}

// One-pole DC blocker / high-pass, corner ~100 Hz at 16 kHz.
class HighPass final {
   public:
    void Reset() {
        previous_in_ = 0;
        previous_out_ = 0;
    }
    void Process(int16_t* samples, uint32_t count) {
        for (uint32_t index = 0U; index < count; ++index) {
            const int32_t input = samples[index];
            // y = x - x1 + 0.961 * y1  (0.961 ~= 984/1024)
            const int32_t output = input - previous_in_ + (previous_out_ * 984) / 1024;
            previous_in_ = input;
            previous_out_ = output;
            samples[index] = static_cast<int16_t>(output > 32767 ? 32767 : (output < -32768 ? -32768 : output));
        }
    }

   private:
    int32_t previous_in_{};
    int32_t previous_out_{};
};

// Noise gate with a self-tracking noise floor. Open when a frame is ~8 dB
// above the floor, stays open through short pauses between words, and fades
// rather than snapping so it does not click.
class NoiseGate final {
   public:
    void Reset() {
        floor_ = 200;
        hold_frames_ = 0U;
        gain_q8_ = 0U;
    }

    void Process(int16_t* samples, uint32_t count) {
        int64_t energy = 0;
        for (uint32_t index = 0U; index < count; ++index) {
            energy += static_cast<int64_t>(samples[index]) * samples[index];
        }
        const int32_t rms = IntSqrt(static_cast<uint32_t>(energy / count));
        // The floor drops to quiet frames quickly; it only rises while the
        // gate is closed (steady background), and slowly, so speech never
        // drags it up and shuts itself off.
        if (rms < floor_) {
            floor_ = (floor_ * 7 + rms) / 8;
        } else if (hold_frames_ == 0U) {
            floor_ += (rms - floor_) / 64;
        }
        // The ES7210 idles near rms 3-5; never let the floor sink below that.
        floor_ = floor_ < 6 ? 6 : floor_;
        if (rms * 2 > floor_ * 5) {
            hold_frames_ = kHoldFrames;
        } else if (hold_frames_ > 0U) {
            --hold_frames_;
        }
        const uint32_t target_q8 = hold_frames_ > 0U ? 256U : kClosedGainQ8;
        const uint32_t start_q8 = gain_q8_;
        for (uint32_t index = 0U; index < count; ++index) {
            const uint32_t gain = start_q8 + (static_cast<int32_t>(target_q8) - static_cast<int32_t>(start_q8)) *
                                                 static_cast<int32_t>(index) / static_cast<int32_t>(count);
            samples[index] = static_cast<int16_t>(static_cast<int32_t>(samples[index]) * static_cast<int32_t>(gain) / 256);
        }
        gain_q8_ = target_q8;
    }

    [[nodiscard]] bool open() const { return hold_frames_ > 0U; }

   private:
    // 250 ms of 10 ms frames.
    static constexpr uint32_t kHoldFrames = 25U;
    // About -24 dB while closed: pauses go quiet without sounding cut off.
    static constexpr uint32_t kClosedGainQ8 = 16U;

    static int32_t IntSqrt(uint32_t value) {
        uint32_t result = 0U;
        uint32_t bit = 1UL << 30U;
        while (bit > value) {
            bit >>= 2U;
        }
        while (bit != 0U) {
            if (value >= result + bit) {
                value -= result + bit;
                result = (result >> 1U) + bit;
            } else {
                result >>= 1U;
            }
            bit >>= 2U;
        }
        return static_cast<int32_t>(result);
    }

    int32_t floor_{200};
    uint32_t hold_frames_{};
    uint32_t gain_q8_{};
};

}  // namespace walkie
