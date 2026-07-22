#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "m2texture/M2TXColor.h"

namespace m2texture {

// Flag/mask constants transcribed from
// reference/sdk-mercury/txtlib/include/M2TXTypes.h. Grouped exactly as the
// original header groups them (Header.Flags vs. TexFormat/DCI.TexelFormat
// both reuse overlapping bit meanings under the M2HC_/M2CI_ prefixes).
namespace HeaderFlags {
constexpr uint32_t IsCompressed = 0x0001;
constexpr uint32_t HasPIP = 0x0002;
constexpr uint32_t HasColorConst = 0x0004;
constexpr uint32_t HasNoDCI = 0x0008;
constexpr uint32_t HasNoTAB = 0x0010;
constexpr uint32_t HasNoDAB = 0x0020;
constexpr uint32_t HasNoLR = 0x0040;
} // namespace HeaderFlags

// M2PG page-header flags (M2PG_FLAGS_* in M2TXTypes.h).
namespace PageFlags {
constexpr uint32_t XWrapMode = 0x01;
constexpr uint32_t YWrapMode = 0x02;
constexpr uint32_t HasTexFormat = 0x04;
constexpr uint32_t HasPIP = 0x08;
constexpr uint32_t HasTAB = 0x10;
constexpr uint32_t HasDAB = 0x20;
constexpr uint32_t HasLR = 0x40;
constexpr uint32_t IsCompressed = 0x80;
} // namespace PageFlags

// Applies to TexFormat (M2TXHeader) and TexelFormat[] entries (M2TXDCI) —
// same bit layout, M2HC_/M2CI_ prefixes in the original are kept as one set
// here since they're bit-for-bit identical.
namespace TexelFlags {
constexpr uint16_t ColorDepth = 0x000f; // literal bits-per-index, 0-8
constexpr uint16_t AlphaDepth = 0x00f0; // literal bits-per-alpha, 0-7 (>>4)
constexpr uint16_t HasSSB = 0x0200;
constexpr uint16_t HasColor = 0x0400;
constexpr uint16_t HasAlpha = 0x0800;
constexpr uint16_t IsLiteral = 0x1000;
constexpr uint16_t IsTrans = 0x0100; // DCI (M2CI) only

inline uint8_t colorDepth(uint16_t f) { return uint8_t(f & ColorDepth); }
inline uint8_t alphaDepth(uint16_t f) { return uint8_t((f & AlphaDepth) >> 4); }
inline bool hasSSB(uint16_t f) { return (f & HasSSB) != 0; }
inline bool hasColor(uint16_t f) { return (f & HasColor) != 0; }
inline bool hasAlpha(uint16_t f) { return (f & HasAlpha) != 0; }
inline bool isLiteral(uint16_t f) { return (f & IsLiteral) != 0; }
} // namespace TexelFlags

constexpr uint32_t kMaxLodNum = 12;

struct TextureHeader {
    uint32_t flags = 0;
    uint16_t minXSize = 0;
    uint16_t minYSize = 0;
    uint16_t texFormat = 0;
    uint8_t numLOD = 0;
    // NOTE: the original M2TXHeader struct also has a `Border` field, but
    // M2TX_WriteChunkData() in the reference encoder never writes it (the
    // 16-byte M2TX chunk it emits is Reserved(4)+Flags(4)+MinX(2)+MinY(2)+
    // TexFormat(2)+NumLOD(1)+Reserved2(1) = 16 bytes, with no Border byte).
    // The reference *reader* nonetheless tries to read a Border byte plus
    // one further "reserved field #2" byte immediately after, i.e. 17
    // bytes from a 16-byte chunk — an off-by-one that reads one byte past
    // the chunk buffer in the original SDK. We don't reproduce that bug:
    // Border is simply not present in files this library round-trips.
};

struct Pip {
    std::vector<uint32_t> colors; // raw M2TXColor entries; decode with decodeM2TXColor()
    uint32_t indexOffset = 0;
};

struct Dci {
    std::array<uint16_t, 4> texelFormat{};
    std::array<uint32_t, 4> txExpColorConst{};
};

struct LodLevel {
    std::vector<uint8_t> data; // raw on-disk texel bytes, format per Dci/TexFormat
};

// In-memory representation of a parsed UTF/M2TX texture. Scope matches the
// M2Suite MVP: header, PIP palette, DCI (compression/format info), and raw
// per-LOD texel bytes are parsed. TAB/DAB (texture/dest-blend attributes),
// LoadRects, M2PG (multi-texture pages) and PCLT (command-list-embedded
// pages, Mercury 3.0+) are out of MVP scope (see plan) and are not parsed.
class Texture {
public:
    // A .utf file holds either one bare FORM TXTR or a 'CAT ' container
    // concatenating several (common in real game data, e.g. 3DOM2VIZ's
    // textures/*.utf). loadAll returns every texture; load returns the
    // first.
    //
    // Page textures (M2PG chunk): the FORM's top-level M2TX header then
    // describes the page *canvas*, not a drawable image — the real images
    // are the page's sub-textures, whose dimensions/LOD-count/format come
    // from the M2PG page headers and whose texel bits live at offsets
    // inside the M2TD blob. loadAll transparently expands each sub-texture
    // into its own standalone Texture (sharing the FORM's PIP), so callers
    // see only drawable textures.
    static std::vector<Texture> loadAll(const uint8_t* data, size_t size);
    static std::vector<Texture> loadAllFromFile(const std::filesystem::path& path);
    static Texture load(const uint8_t* data, size_t size);
    static Texture loadFromFile(const std::filesystem::path& path);

    const TextureHeader& header() const { return header_; }
    const Pip& pip() const { return pip_; }
    const Dci& dci() const { return dci_; }
    bool hasDci() const { return hasDci_; }
    const std::vector<LodLevel>& lods() const { return lods_; }

    bool isCompressed() const { return (header_.flags & HeaderFlags::IsCompressed) != 0; }
    // PIP presence is determined by the M2PI chunk existing, exactly as the
    // reference reader does (ProcessTXTR sets the flag from chunk presence
    // — real game files often have flags == 0 yet carry a PIP).
    bool hasPip() const { return hasPip_; }

    // Per-LOD dimensions. MinXSize/MinYSize are the *coarsest* level; LOD 0
    // is the finest: dim(lod) = min << (numLOD - lod - 1), per
    // M2TXHeader_GetLODDim in the reference source.
    uint32_t lodWidth(size_t lodIndex) const;
    uint32_t lodHeight(size_t lodIndex) const;

    // Decodes one LOD level to a flat top-to-bottom RGBA8 buffer of
    // lodWidth(i) x lodHeight(i).
    //
    // Uncompressed textures: full support for the M2 texel bitstream
    // (MSB-first, per pixel: [SSB:1][Alpha:aDepth][literal RGB 3xcDepth |
    // PIP index cDepth]) at any depth — port of the uncompressed branch of
    // the reference's LOD walker in M2TXLibrary.c.
    //
    // RLE-compressed textures: decoded via the DCI control-byte scheme
    // (2-bit texel-format select + run/string counts) from the same
    // reference walker. Compressed textures require a DCI chunk.
    std::vector<Rgba8> decodeLodToRgba(size_t lodIndex) const;

private:
    TextureHeader header_;
    Pip pip_;
    Dci dci_;
    bool hasDci_ = false;
    bool hasPip_ = false;
    std::vector<LodLevel> lods_;
};

} // namespace m2texture
