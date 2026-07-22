#pragma once

#include <cstdint>

namespace m2texture {

// M2TXColor: the 32-bit packed color used throughout the UTF/M2TX format —
// most importantly as each entry of the PIP (palette) chunk. Verified
// against the reference implementation's M2TXColor_Create()/_Decode() in
// reference/sdk-mercury/txtlib/src/M2TXLibrary.c:
//
//   bit 31      : SSB   (1 bit, "super side band" / stencil bit)
//   bits 24-30  : Alpha (7 bits, stored)
//   bits 16-23  : Red   (8 bits)
//   bits 8-15   : Green (8 bits)
//   bits 0-7    : Blue  (8 bits)
//
// Alpha is stored 7-bit but exposed as 8-bit by replicating the low bit
// into the new LSB on decode (`alpha8 = (alpha7 << 1) | (alpha7 & 1)`) —
// this is exactly what M2TXColor_Decode() does, reproduced here rather
// than reinvented.
struct Rgba8 {
    uint8_t r = 0, g = 0, b = 0, a = 0;
    bool ssb = false;
};

inline Rgba8 decodeM2TXColor(uint32_t packed) {
    Rgba8 c;
    c.b = uint8_t(packed & 0xFF);
    c.g = uint8_t((packed >> 8) & 0xFF);
    c.r = uint8_t((packed >> 16) & 0xFF);
    uint8_t a7 = uint8_t((packed >> 24) & 0x7F);
    c.a = uint8_t((a7 << 1) | (a7 & 0x01));
    c.ssb = (packed >> 31) & 0x1;
    return c;
}

inline uint32_t encodeM2TXColor(const Rgba8& c) {
    uint32_t v = uint32_t(c.ssb ? 1u : 0u) << 31;
    v |= (uint32_t(c.a) & 0xFEu) << 23;
    v |= uint32_t(c.r) << 16;
    v |= uint32_t(c.g) << 8;
    v |= uint32_t(c.b);
    return v;
}

} // namespace m2texture
