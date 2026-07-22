#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "m2texture/M2TXColor.h"

namespace m2texture {

// Encodes an RGBA8 image into an M2 UTF texture (FORM TXTR), the reverse of
// Texture::load. Emits the simplest broadly-compatible form: a single-LOD,
// uncompressed, *literal* (non-palettised) texture, so no PIP is needed and
// any source image can be represented without colour quantisation.
//
// On-disk layout produced (all big-endian, IFF chunks 4-byte aligned):
//   FORM <size> TXTR
//     M2TX 16 : Reserved(4) Flags(4) MinX(2) MinY(2) TexFormat(2)
//               NumLOD(1) Reserved2(1)
//     M2TD <n>: the texel bitstream
//
// The texel bitstream is MSB-first and, per pixel, matches what the decoder
// expects: [SSB:1 if present][Alpha:alphaDepth][R,G,B each colorDepth].
// With NumLOD == 1 the header's MinXSize/MinYSize are the image dimensions
// (dim(lod) = min << (numLOD - lod - 1)).
struct EncodeOptions {
    // Bits per RGB component (1..8). 8 is lossless for 8-bit sources.
    uint8_t colorDepth = 8;
    // Bits of alpha (0 = no alpha channel stored, max 7 on M2). When the
    // source has no meaningful transparency, leaving this 0 halves nothing
    // but keeps the texture simpler and smaller.
    uint8_t alphaDepth = 0;
};

// Chooses sensible options for an image: enables alpha only when the source
// actually contains non-opaque pixels.
EncodeOptions defaultEncodeOptions(const Rgba8* pixels, uint32_t width, uint32_t height);

// Builds the complete .utf file bytes.
std::vector<uint8_t> encodeUtf(const Rgba8* pixels, uint32_t width, uint32_t height,
                                const EncodeOptions& options);

// Convenience: encode and write to disk.
void writeUtfFile(const std::filesystem::path& path, const Rgba8* pixels, uint32_t width,
                   uint32_t height, const EncodeOptions& options);

} // namespace m2texture
