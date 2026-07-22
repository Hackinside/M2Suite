#pragma once

#include <cstddef>
#include <cstdint>

namespace m2core {

// MSB-first bit reader over a byte buffer — the bit order used by M2's
// texel data streams (port of qMemGetBits in txtlib/src/qmem.c: bits are
// consumed from each byte's most-significant end, continuously across
// bytes, with no row alignment; only whole LODs are padded).
class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    // Reads `count` bits (0..32) MSB-first. Reading past the end returns
    // zero bits (mirrors the reference's permissive behavior — the final
    // LOD bytes are zero-padded, so trailing reads see zeros) but sets
    // overran() so callers can detect truncated data.
    uint32_t read(uint8_t count);

    bool overran() const { return overran_; }
    size_t bitPosition() const { return bitPos_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t bitPos_ = 0;
    bool overran_ = false;
};

inline uint32_t BitReader::read(uint8_t count) {
    uint32_t v = 0;
    for (uint8_t i = 0; i < count; ++i) {
        size_t byteIndex = bitPos_ >> 3;
        uint32_t bit = 0;
        if (byteIndex < size_) {
            bit = (data_[byteIndex] >> (7 - (bitPos_ & 7))) & 1;
        } else {
            overran_ = true;
        }
        v = (v << 1) | bit;
        ++bitPos_;
    }
    return v;
}

} // namespace m2core
