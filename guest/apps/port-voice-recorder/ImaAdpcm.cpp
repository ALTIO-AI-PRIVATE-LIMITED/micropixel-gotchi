#include "ImaAdpcm.h"

namespace ima {

namespace {

const int16_t kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
    19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
    5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

const int8_t kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8,
                                -1, -1, -1, -1, 2, 4, 6, 8};

constexpr int kMaxIndex = 88;

int clampIndex(int index) {
    return index < 0 ? 0 : (index > kMaxIndex ? kMaxIndex : index);
}

int clampSample(int value) {
    return value < -32768 ? -32768 : (value > 32767 ? 32767 : value);
}

// The difference a code stands for at a given step, exactly as a decoder
// computes it. The encoder uses the same arithmetic so that its predictor
// never drifts away from the decoder's.
int deltaFor(uint8_t code, int step) {
    int delta = step >> 3;
    if (code & 4) {
        delta += step;
    }
    if (code & 2) {
        delta += step >> 1;
    }
    if (code & 1) {
        delta += step >> 2;
    }
    return (code & 8) ? -delta : delta;
}

uint8_t encodeSample(int sample, int &predictor, int &index) {
    const int step = kStepTable[index];
    int diff = sample - predictor;
    uint8_t code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    if (diff >= step) {
        code |= 4;
        diff -= step;
    }
    if (diff >= (step >> 1)) {
        code |= 2;
        diff -= step >> 1;
    }
    if (diff >= (step >> 2)) {
        code |= 1;
    }

    predictor = clampSample(predictor + deltaFor(code, step));
    index = clampIndex(index + kIndexTable[code]);
    return code;
}

int decodeSample(uint8_t code, int &predictor, int &index) {
    predictor = clampSample(predictor + deltaFor(code, kStepTable[index]));
    index = clampIndex(index + kIndexTable[code]);
    return predictor;
}

} // namespace

void Encoder::encodeBlock(const int16_t *samples, uint8_t *out) {
    // The header stores the first sample exactly, so each block starts
    // from a known predictor and an error cannot outlive its block.
    int predictor = samples[0];
    out[0] = static_cast<uint8_t>(predictor & 0xFF);
    out[1] = static_cast<uint8_t>((predictor >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>(index_);
    out[3] = 0;

    uint8_t *dst = out + kHeaderBytes;
    for (size_t i = 1; i < kSamplesPerBlock; i += 2) {
        const uint8_t low = encodeSample(samples[i], predictor, index_);
        const uint8_t high = encodeSample(samples[i + 1], predictor, index_);
        *dst++ = static_cast<uint8_t>(low | (high << 4));
    }
}

bool decodeBlock(const uint8_t *in, int16_t *out) {
    int predictor = static_cast<int16_t>(in[0] | (in[1] << 8));
    int index = in[2];
    if (index > kMaxIndex) {
        return false;
    }

    out[0] = static_cast<int16_t>(predictor);
    const uint8_t *src = in + kHeaderBytes;
    for (size_t i = 1; i < kSamplesPerBlock; i += 2) {
        const uint8_t byte = *src++;
        out[i] = static_cast<int16_t>(decodeSample(byte & 0x0F, predictor, index));
        out[i + 1] = static_cast<int16_t>(decodeSample(byte >> 4, predictor, index));
    }
    return true;
}

} // namespace ima
