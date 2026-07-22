#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "m2texture/M2TXColor.h" // Rgba8

namespace m2cel {

// Classic 3DO cel image (.cel), as produced for the original 3DO (M1) and
// still used on M2 discs (fixture: misc/Message.cel from the M2 demo disc
// dump). On-disk layout is a big-endian chunk sequence:
//
//   'CCB ' <size:u32> version flags nextptr sourceptr plutptr xpos ypos
//          hdx hdy vdx vdy hddx hddy pixc pre0 pre1 width height   (80 bytes)
//   'PDAT' <size:u32> <pixel data>
//   'PLUT' <size:u32> <numEntries:u32> <entries: u16 each, 15-bit RGB 5-5-5>
//
// Verified against misc/Message.cel: CCB size 0x50, width 206, height 72,
// PRE0 bpp code 5 (8bpp), coded (PLUT-indexed), CCB_PACKED flag set,
// 32-entry PLUT.
//
// Decode scope (MVP): coded and uncoded 8bpp, packed (RLE) and unpacked,
// and uncoded 16bpp. Sub-byte bit depths (1/2/4/6bpp) throw
// m2core::NotImplementedError until needed by a real fixture.

// ccb_Flags bits we act on (others parsed but unused for decoding).
constexpr uint32_t CCB_PACKED = 0x00000200;

struct CcbHeader {
    uint32_t version = 0;
    uint32_t flags = 0;
    uint32_t nextPtr = 0;
    uint32_t sourcePtr = 0;
    uint32_t plutPtr = 0;
    int32_t xPos = 0;
    int32_t yPos = 0;
    int32_t hdx = 0, hdy = 0, vdx = 0, vdy = 0, hddx = 0, hddy = 0;
    uint32_t pixc = 0;
    uint32_t pre0 = 0;
    uint32_t pre1 = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    bool isPacked() const { return (flags & CCB_PACKED) != 0; }
    // PRE0 bits 0-2: bits-per-pixel code (1,2,3,4,5,6 -> 1,2,4,6,8,16 bpp)
    uint8_t bppCode() const { return uint8_t(pre0 & 0x7); }
    uint8_t bitsPerPixel() const;
    // PRE0 bit 4: set = uncoded (literal color), clear = coded (PLUT index)
    bool isUncoded() const { return (pre0 & 0x10) != 0; }
};

class Cel {
public:
    static Cel load(const uint8_t* data, size_t size);
    static Cel loadFromFile(const std::filesystem::path& path);

    const CcbHeader& ccb() const { return ccb_; }
    const std::vector<uint8_t>& pixelData() const { return pdat_; }
    const std::vector<uint16_t>& plut() const { return plut_; }

    // Decodes to a flat top-to-bottom RGBA8 buffer of ccb().width x
    // ccb().height. Transparent (skipped) pixels get alpha 0.
    std::vector<m2texture::Rgba8> decodeToRgba() const;

private:
    // Builds a Cel from a headerless raw 16bpp (0555) framebuffer — some
    // game cels are just a raw 320-wide screen dump with no CCB chunk.
    static Cel makeRaw(const uint8_t* data, size_t size);

    CcbHeader ccb_;
    std::vector<uint8_t> pdat_;
    std::vector<uint16_t> plut_;
};

// Expands a 15-bit 5-5-5 PLUT entry to RGBA8 (alpha 255). 5->8 bit
// expansion replicates the top bits into the bottom ((v<<3)|(v>>2)) so
// full-scale white maps to 255 rather than 248.
m2texture::Rgba8 expandPlutEntry(uint16_t entry);

// Decodes one cel frame (CCB + pixel data + optional PLUT) to RGBA8.
// Shared by Cel::decodeToRgba and Anim's per-frame decode.
std::vector<m2texture::Rgba8> decodeCelFrame(const CcbHeader& ccb,
                                              const std::vector<uint8_t>& pdat,
                                              const std::vector<uint16_t>& plut);

} // namespace m2cel
