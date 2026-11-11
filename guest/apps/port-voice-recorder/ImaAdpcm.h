#pragma once

#include <stddef.h>
#include <stdint.h>

// IMA ADPCM in the WAV layout (format tag 0x11, "DVI ADPCM"), mono.
//
// Each block starts with a 4-byte header: the first sample as a
// little-endian int16, the step index, and a zero byte. The remaining
// samples follow as 4-bit codes, two per byte, the earlier sample in the
// low nibble. With 256-byte blocks that is 1 + 252 * 2 = 505 samples.
//
// 4 bits per sample is a quarter of 16-bit PCM, and encoding costs a few
// additions and shifts per sample, so the recorder spends almost no CPU on
// it. Players that understand WAV ADPCM: VLC, Windows, macOS CoreAudio.
namespace ima {

constexpr size_t kBlockBytes = 256;
constexpr size_t kHeaderBytes = 4;
constexpr size_t kSamplesPerBlock = 1 + (kBlockBytes - kHeaderBytes) * 2;

// Encodes blocks one after another. The step index carries over from one
// block to the next, so a recording is encoded by a single Encoder.
class Encoder {
  public:
    // samples: kSamplesPerBlock samples; out: kBlockBytes bytes.
    void encodeBlock(const int16_t *samples, uint8_t *out);

    void reset() { index_ = 0; }

  private:
    int index_ = 0;
};

// Decodes one block. Returns false if the header's step index is out of
// range, i.e. the bytes are not an IMA ADPCM block.
bool decodeBlock(const uint8_t *in, int16_t *out);

} // namespace ima
