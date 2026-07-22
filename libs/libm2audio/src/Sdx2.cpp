#include "m2audio/Sdx2.h"

#include <algorithm>

namespace m2audio {

namespace {
// Shared decode loop, matching vgmstream's decode_delta_exact: even bytes
// reset the history (absolute value), the table value is always added.
std::vector<int16_t> decodeDeltaExact(const uint8_t* data, size_t size, uint32_t channels,
                                       const int16_t* table) {
    if (channels == 0 || channels > 8) {
        channels = 1;
    }
    std::vector<int16_t> out;
    out.reserve(size);
    int32_t hist[8] = {};
    uint32_t ch = 0;

    for (size_t i = 0; i < size; ++i) {
        uint8_t b = data[i];
        if (!(b & 1)) {
            hist[ch] = 0; // even: exact mode
        }
        int32_t sample = std::clamp(hist[ch] + table[b], -32768, 32767);
        hist[ch] = sample;
        out.push_back(int16_t(sample));
        ch = (ch + 1) % channels;
    }
    return out;
}

struct Tables {
    int16_t squares[256];
    int16_t cubes[256];
    Tables() {
        for (int i = -128; i < 128; ++i) {
            int32_t sq = i < 0 ? -(i * i) * 2 : (i * i) * 2;
            squares[uint8_t(i)] = int16_t(std::clamp(sq, -32768, 32767));
            // Cube table per the DSP ops documented in vgmstream's
            // sdx2_decoder.c: v = i*256; a = (v*v)>>15; a = (a*v)>>15 —
            // arithmetic shifts, i.e. floor. Equivalent to floor(i^3 / 64).
            int64_t v = int64_t(i) * 256;
            int64_t a = (v * v) >> 15;
            a = (a * v) >> 15; // v*v is non-negative; a*v may be negative:
                                // >> on negative is floor on all targets we
                                // build for (MSVC/GCC/Clang arithmetic shift)
            cubes[uint8_t(i)] = int16_t(std::clamp<int64_t>(a, -32768, 32767));
        }
    }
};
const Tables& tables() {
    static const Tables t;
    return t;
}
} // namespace

std::vector<int16_t> decodeSdx2(const uint8_t* data, size_t size, uint32_t channels) {
    return decodeDeltaExact(data, size, channels, tables().squares);
}

std::vector<int16_t> decodeCbd2(const uint8_t* data, size_t size, uint32_t channels) {
    return decodeDeltaExact(data, size, channels, tables().cubes);
}

namespace {
// IMA/DVI ADPCM tables. The step table is transcribed from
// StepSizeInitData in the audio folio's sub_decode_adp4.ins, which matches
// the canonical 89-entry IMA table exactly.
constexpr int16_t kImaStep[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,
    21,    23,    25,    28,    31,    34,    37,    41,    45,    50,    55,
    60,    66,    73,    80,    88,    97,    107,   118,   130,   143,   157,
    173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,
    494,   544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,
    1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,  3660,
    4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767};
constexpr int kImaIndexAdjust[16] = {-1, -1, -1, -1, 2, 4, 6, 8,
                                      -1, -1, -1, -1, 2, 4, 6, 8};

struct ImaState {
    int32_t predictor = 0;
    int32_t index = 0;
};

int16_t imaDecodeNibble(ImaState& st, uint8_t nibble) {
    int32_t step = kImaStep[st.index];
    // diff = step/8 + (bit0 ? step/4 : 0) + (bit1 ? step/2 : 0) + (bit2 ? step : 0)
    int32_t diff = step >> 3;
    if (nibble & 1) diff += step >> 2;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 4) diff += step;
    st.predictor += (nibble & 8) ? -diff : diff;
    st.predictor = std::clamp(st.predictor, -32768, 32767);
    st.index = std::clamp(st.index + kImaIndexAdjust[nibble & 0x0F], 0, 88);
    return int16_t(st.predictor);
}
} // namespace

std::vector<int16_t> decodeAdp4(const uint8_t* data, size_t size, uint32_t channels) {
    if (channels == 0) {
        channels = 1;
    }
    std::vector<ImaState> state(channels);
    std::vector<int16_t> out;
    out.reserve(size * 2);
    // Two samples per byte, high nibble first, cycling through channels so
    // interleaved stereo keeps per-channel predictor state.
    uint32_t ch = 0;
    for (size_t i = 0; i < size; ++i) {
        uint8_t byte = data[i];
        out.push_back(imaDecodeNibble(state[ch], uint8_t(byte >> 4)));
        ch = (ch + 1) % channels;
        out.push_back(imaDecodeNibble(state[ch], uint8_t(byte & 0x0F)));
        ch = (ch + 1) % channels;
    }
    return out;
}

} // namespace m2audio
