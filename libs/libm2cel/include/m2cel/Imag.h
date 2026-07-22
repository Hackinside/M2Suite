#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "m2texture/M2TXColor.h" // Rgba8

namespace m2cel {

// 3DO IMAG image file (form3do.h ImageCC): an 'IMAG' control chunk plus a
// 'PDAT' pixel chunk. Used for M1-era full screens/backdrops (fixture:
// strahl's ending screens, 320x239 16-bit).
//
// Decode scope: 16-bit 0555 pixels, linear or "Sherrie LRForm" order
// (pixelorder 1: two vertically adjacent rows interleaved word-by-word —
// the M1 VRAM layout). Other depths throw m2core::NotImplementedError.
class Imag {
public:
    static Imag load(const uint8_t* data, size_t size);
    static Imag loadFromFile(const std::filesystem::path& path);

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bytesPerRow = 0;
    uint8_t bitsPerPixel = 0;
    uint8_t numComponents = 0;
    uint8_t numPlanes = 0;
    uint8_t colorSpace = 0;
    uint8_t compType = 0;
    uint8_t hvFormat = 0;
    uint8_t pixelOrder = 0; // 0 = linear, 1 = Sherrie LRForm, 2 = UGO LRForm
    uint8_t version = 0;

    const std::vector<uint8_t>& pixelData() const { return pdat_; }

    std::vector<m2texture::Rgba8> decodeToRgba() const;

private:
    std::vector<uint8_t> pdat_;
};

} // namespace m2cel
