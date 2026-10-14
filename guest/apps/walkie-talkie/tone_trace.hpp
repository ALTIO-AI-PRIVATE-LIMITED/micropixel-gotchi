// SPDX-License-Identifier: Apache-2.0
//
// Diagnostic: labels each 20 ms frame by which of three test notes (600,
// 1000, 1500 Hz) dominates it, so a log line shows what the microphone heard
// on the talker and what the decoder produced on the listener. '.' is quiet,
// 'A'/'B'/'C' a test note, '~' any other sound such as speech.
#pragma once

#include <stdint.h>

namespace walkie {

class ToneTrace final {
   public:
    void Reset() {
        length_ = 0U;
        text_[0] = '\0';
    }

    // Frames are 16 kHz: the Goertzel coefficients below are 2cos(2 pi f / 16000).
    void Add(const int16_t* samples, uint32_t count) {
        if (length_ + 1U >= sizeof(text_)) {
            return;
        }
        double energy = 0.0;
        for (uint32_t index = 0U; index < count; ++index) {
            energy += static_cast<double>(samples[index]) * samples[index];
        }
        char label = '.';
        // About -50 dBFS RMS: quieter than this is room noise.
        if (energy / count > 100.0 * 100.0) {
            constexpr double kCoefficients[3] = {1.944739840795, 1.847759065023, 1.662939224605};
            double best = 0.0;
            uint32_t best_note = 0U;
            for (uint32_t note = 0U; note < 3U; ++note) {
                const double power = Goertzel(samples, count, kCoefficients[note]);
                if (power > best) {
                    best = power;
                    best_note = note;
                }
            }
            // A pure note puts about half of N * energy into its bin.
            label = best > 0.25 * count * energy ? static_cast<char>('A' + best_note) : '~';
        }
        text_[length_++] = label;
        text_[length_] = '\0';
    }

    [[nodiscard]] const char* c_str() const { return text_; }

   private:
    static double Goertzel(const int16_t* samples, uint32_t count, double coefficient) {
        double previous = 0.0;
        double before = 0.0;
        for (uint32_t index = 0U; index < count; ++index) {
            const double current = samples[index] + coefficient * previous - before;
            before = previous;
            previous = current;
        }
        return previous * previous + before * before - coefficient * previous * before;
    }

    char text_[520]{};
    uint32_t length_{};
};

}  // namespace walkie
