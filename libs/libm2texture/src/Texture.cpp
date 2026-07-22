#include "m2texture/Texture.h"

#include <array>
#include <fstream>
#include <string>

#include "m2core/BitReader.h"
#include "m2core/Error.h"
#include "m2core/Iff.h"

namespace m2texture {

using m2core::BitReader;
using m2core::ByteReader;
using m2core::FormatError;
using m2core::IffForm;
using m2core::NotImplementedError;

namespace {
// Chunk IDs, transcribed from M2TXiff.c (ID_TXTR/ID_M2TX/etc. #defines).
constexpr uint32_t ID_TXTR = m2core::makeId('T', 'X', 'T', 'R');
constexpr uint32_t ID_M2TX = m2core::makeId('M', '2', 'T', 'X');
constexpr uint32_t ID_M2PI = m2core::makeId('M', '2', 'P', 'I');
constexpr uint32_t ID_M2CI = m2core::makeId('M', '2', 'C', 'I');
constexpr uint32_t ID_M2TD = m2core::makeId('M', '2', 'T', 'D');
constexpr uint32_t ID_M2PG = m2core::makeId('M', '2', 'P', 'G');

// Bits per stored texel for a given texel format (SSB + alpha + color
// bits; literal color is three components).
size_t bitsPerTexel(uint16_t format) {
    size_t bits = 0;
    if (TexelFlags::hasSSB(format)) bits += 1;
    if (TexelFlags::hasAlpha(format)) bits += TexelFlags::alphaDepth(format);
    if (TexelFlags::hasColor(format)) {
        size_t c = TexelFlags::colorDepth(format);
        bits += TexelFlags::isLiteral(format) ? 3 * c : c;
    }
    return bits;
}

// One M2PG page sub-texture header (M2TXPgHeader), parsed per the
// version>=1 branch of ProcessTXTR in M2TXiff.c.
struct PageEntry {
    uint32_t offset = 0;
    uint32_t pgFlags = 0;
    uint16_t minXSize = 0;
    uint16_t minYSize = 0;
    uint16_t texFormat = 0;
    uint8_t numLOD = 0;
    uint8_t pipIndexOffset = 0;
    std::array<uint16_t, 4> texelFormat{};
    std::array<uint32_t, 4> txExpColorConst{};
    std::array<uint32_t, kMaxLodNum> lodLength{};
};

std::vector<PageEntry> parseM2PG(const std::vector<uint8_t>& chunkData) {
    if (chunkData.size() < 8) {
        throw FormatError("M2PG chunk smaller than 8 bytes");
    }
    ByteReader r(chunkData);
    uint32_t numTex = r.readU32BE();
    uint32_t version = r.readU32BE();
    std::vector<PageEntry> pages(numTex);
    for (auto& pg : pages) {
        pg.offset = r.readU32BE();
        pg.pgFlags = r.readU32BE();
        pg.minXSize = r.readU16BE();
        pg.minYSize = r.readU16BE();
        pg.texFormat = r.readU16BE();
        pg.numLOD = r.readU8();
        pg.pipIndexOffset = r.readU8();
        if (pg.numLOD > kMaxLodNum) {
            throw FormatError("M2PG sub-texture claims more LODs than MAX_LOD_NUM");
        }
        if (version < 1) {
            // Pre-Mercury-3.0 layout: four extra per-page index bytes
            // (PgPIPIndex/PgTABIndex/PgDABIndex/PgLRIndex — unused since
            // Mercury 3.0), and no compression table (per the version<1
            // branch of ProcessTXTR in M2TXiff.c).
            r.skip(4);
        } else if (pg.pgFlags & PageFlags::IsCompressed) {
            for (auto& f : pg.texelFormat) f = r.readU16BE();
            for (auto& c : pg.txExpColorConst) c = r.readU32BE();
            for (uint8_t j = 0; j < pg.numLOD; ++j) pg.lodLength[j] = r.readU32BE();
        }
    }
    return pages;
}

TextureHeader parseM2TX(const std::vector<uint8_t>& chunkData) {
    // 16 bytes on disk: Reserved(4) Flags(4) MinXSize(2) MinYSize(2)
    // TexFormat(2) NumLOD(1) Reserved2(1). See Texture.h for why this is
    // 16 bytes and not the 17 the reference *reader* (buggily) attempts.
    if (chunkData.size() < 16) {
        throw FormatError("M2TX chunk smaller than 16 bytes");
    }
    ByteReader r(chunkData);
    r.skip(4); // reserved
    TextureHeader h;
    h.flags = r.readU32BE();
    h.minXSize = r.readU16BE();
    h.minYSize = r.readU16BE();
    h.texFormat = r.readU16BE();
    h.numLOD = r.readU8();
    // byte 15 (reference's "Border") is intentionally not read as
    // meaningful data — see Texture.h note.
    return h;
}

Pip parseM2PI(const std::vector<uint8_t>& chunkData) {
    if (chunkData.size() < 8) {
        throw FormatError("M2PI (PIP) chunk smaller than 8 bytes");
    }
    ByteReader r(chunkData);
    Pip pip;
    pip.indexOffset = r.readU32BE();
    r.skip(4); // reserved
    size_t numColors = (chunkData.size() - 8) / 4;
    pip.colors.reserve(numColors);
    for (size_t i = 0; i < numColors; ++i) {
        pip.colors.push_back(r.readU32BE());
    }
    return pip;
}

Dci parseM2CI(const std::vector<uint8_t>& chunkData) {
    if (chunkData.size() < 24) {
        throw FormatError("M2CI (DCI) chunk smaller than 24 bytes");
    }
    ByteReader r(chunkData);
    Dci dci;
    for (auto& f : dci.texelFormat) f = r.readU16BE();
    for (auto& c : dci.txExpColorConst) c = r.readU32BE();
    return dci;
}

// M2TD layout: NumLOD(u16) Reserved(u16) LODDataOffset[NumLOD](u32 each),
// followed immediately by all LOD texel bytes back-to-back in order. The
// offsets are only used to derive each LOD's byte length (nextOffset -
// thisOffset); the reference implementation never seeks by them, it just
// keeps reading sequentially — see Texel_ComputeSizeOffsets() in
// M2TXiff.c.
std::vector<LodLevel> parseM2TD(const std::vector<uint8_t>& chunkData, uint8_t headerNumLOD) {
    if (chunkData.size() < 4) {
        throw FormatError("M2TD (texel data) chunk smaller than 4 bytes");
    }
    ByteReader r(chunkData);
    uint16_t numLOD = r.readU16BE();
    r.skip(2); // reserved

    (void)headerNumLOD; // reference only warns on mismatch; M2TD wins
    if (numLOD > kMaxLodNum) {
        throw FormatError("M2TD chunk claims more LODs than MAX_LOD_NUM");
    }

    std::vector<uint32_t> offsets(numLOD);
    for (auto& off : offsets) off = r.readU32BE();

    std::vector<LodLevel> lods(numLOD);
    for (size_t i = 0; i < numLOD; ++i) {
        uint32_t nextOff = (i + 1 < numLOD) ? offsets[i + 1] : uint32_t(chunkData.size());
        if (nextOff < offsets[i]) {
            throw FormatError("M2TD chunk has non-increasing LOD offsets");
        }
        uint32_t lodSize = nextOff - offsets[i];
        lods[i].data = r.readBytes(lodSize);
    }
    return lods;
}

// Expands a `depth`-bit component to 8 bits by left-shift + bit
// replication (so full-scale maps to 255, not 255 - (255 >> depth)).
uint8_t expandComponent(uint32_t v, uint8_t depth) {
    if (depth == 0) return 0;
    if (depth >= 8) return uint8_t(v);
    uint32_t out = v << (8 - depth);
    uint8_t filled = depth;
    while (filled < 8) {
        out |= out >> filled;
        filled = uint8_t(filled * 2);
    }
    return uint8_t(out);
}

// Decodes a single texel from the bitstream using one texel format
// (header TexFormat for uncompressed textures, or a DCI TexelFormat entry
// for compressed runs). Port of the per-pixel body of the reference LOD
// walker in M2TXLibrary.c (both branches read SSB, then alpha, then
// literal RGB or PIP index, in that order).
Rgba8 decodeTexel(BitReader& bits, uint16_t format, const Pip& pip, bool pipPresent) {
    Rgba8 out;
    bool ssb = false;
    bool haveAlpha = false;
    uint8_t alpha = 255;

    if (TexelFlags::hasSSB(format)) {
        ssb = bits.read(1) != 0;
    }
    uint8_t aDepth = TexelFlags::alphaDepth(format);
    if (TexelFlags::hasAlpha(format) && aDepth > 0) {
        alpha = expandComponent(bits.read(aDepth), aDepth);
        haveAlpha = true;
    }

    uint8_t cDepth = TexelFlags::colorDepth(format);
    if (TexelFlags::hasColor(format) && cDepth > 0) {
        if (TexelFlags::isLiteral(format)) {
            out.r = expandComponent(bits.read(cDepth), cDepth);
            out.g = expandComponent(bits.read(cDepth), cDepth);
            out.b = expandComponent(bits.read(cDepth), cDepth);
            out.a = 255;
        } else {
            uint32_t index = bits.read(cDepth);
            if (!pipPresent) {
                throw FormatError("indexed texel format but no PIP palette present");
            }
            if (index >= pip.colors.size()) {
                throw FormatError("texel PIP index out of range");
            }
            out = decodeM2TXColor(pip.colors[index]);
        }
    } else {
        out.a = 255;
    }

    if (haveAlpha) {
        out.a = alpha;
    }
    if (TexelFlags::hasSSB(format)) {
        out.ssb = ssb;
    }
    return out;
}
} // namespace

std::vector<Texture> Texture::loadAll(const uint8_t* data, size_t size) {
    ByteReader reader(data, size);
    std::vector<IffForm> forms = IffForm::parseAll(reader);

    std::vector<Texture> textures;
    for (const IffForm& form : forms) {
        if (form.formType() != ID_TXTR) {
            throw FormatError("not a UTF/M2TX texture FORM: type is '" +
                               m2core::idToString(form.formType()) + "', expected 'TXTR'");
        }
        Texture tex;

        const auto* m2tx = form.find(ID_M2TX);
        if (!m2tx) {
            throw FormatError("UTF/M2TX FORM has no M2TX header chunk");
        }
        tex.header_ = parseM2TX(*m2tx);

        if (const auto* m2pi = form.find(ID_M2PI)) {
            tex.pip_ = parseM2PI(*m2pi);
            tex.hasPip_ = true;
        }
        if (const auto* m2ci = form.find(ID_M2CI)) {
            tex.dci_ = parseM2CI(*m2ci);
            tex.hasDci_ = true;
        }
        if (const auto* m2td = form.find(ID_M2TD)) {
            tex.lods_ = parseM2TD(*m2td, tex.header_.numLOD);
        }

        // Page texture: expand each sub-texture into a standalone Texture
        // carved out of the page's texel blob (see loadAll doc comment).
        const auto* m2pg = form.find(ID_M2PG);
        if (m2pg && !tex.lods_.empty()) {
            std::vector<PageEntry> pages = parseM2PG(*m2pg);
            const std::vector<uint8_t>& blob = tex.lods_[0].data;
            for (const PageEntry& pg : pages) {
                Texture sub;
                sub.header_ = tex.header_;
                sub.header_.minXSize = pg.minXSize;
                sub.header_.minYSize = pg.minYSize;
                sub.header_.numLOD = pg.numLOD;
                if (pg.pgFlags & PageFlags::HasTexFormat) {
                    sub.header_.texFormat = pg.texFormat;
                }
                sub.pip_ = tex.pip_;
                sub.hasPip_ = tex.hasPip_;

                if (pg.offset > blob.size()) {
                    throw FormatError("M2PG sub-texture offset extends past texel data");
                }
                size_t pos = pg.offset;
                if (pg.pgFlags & PageFlags::IsCompressed) {
                    sub.header_.flags |= HeaderFlags::IsCompressed;
                    if (pg.lodLength[0] == 0) {
                        // Version 0 pages predate the per-page DCI table —
                        // compression info comes from the FORM's M2CI chunk.
                        sub.dci_ = tex.dci_;
                        sub.hasDci_ = tex.hasDci_;
                    } else {
                        for (int i = 0; i < 4; ++i) {
                            sub.dci_.texelFormat[size_t(i)] = pg.texelFormat[size_t(i)];
                            sub.dci_.txExpColorConst[size_t(i)] = pg.txExpColorConst[size_t(i)];
                        }
                        sub.hasDci_ = true;
                    }
                    if (pg.lodLength[0] == 0) {
                        // M2PG version 0: no per-LOD length table. Give the
                        // sub-texture everything up to the next page's
                        // offset (or blob end); the RLE decoder stops after
                        // the finest LOD's pixel count, so LOD 0 decodes
                        // correctly. Coarser LODs would need sequential
                        // stream continuation — omitted until a fixture
                        // needs them.
                        size_t end = blob.size();
                        for (const PageEntry& other : pages) {
                            if (other.offset > pg.offset && other.offset < end) {
                                end = other.offset;
                            }
                        }
                        LodLevel lod;
                        lod.data.assign(blob.begin() + pos, blob.begin() + end);
                        sub.lods_.push_back(std::move(lod));
                    } else {
                        for (uint8_t i = 0; i < pg.numLOD; ++i) {
                            size_t len = pg.lodLength[i];
                            if (pos + len > blob.size()) {
                                throw FormatError("M2PG compressed LOD extends past texel data");
                            }
                            LodLevel lod;
                            lod.data.assign(blob.begin() + pos, blob.begin() + pos + len);
                            sub.lods_.push_back(std::move(lod));
                            pos += len;
                        }
                    }
                } else {
                    sub.header_.flags &= ~HeaderFlags::IsCompressed;
                    // Uncompressed sub-LODs are stored finest-first,
                    // back-to-back, each byte-padded then 4-byte-aligned
                    // (per the reference LOD writer's end-of-LOD padding in
                    // M2TXLibrary.c).
                    size_t bpt = bitsPerTexel(sub.header_.texFormat);
                    for (uint8_t i = 0; i < pg.numLOD; ++i) {
                        size_t pixels = (size_t(pg.minXSize) << (pg.numLOD - i - 1)) *
                                        (size_t(pg.minYSize) << (pg.numLOD - i - 1));
                        size_t bytes = (pixels * bpt + 7) / 8;
                        bytes = (bytes + 3) & ~size_t(3);
                        if (pos + bytes > blob.size()) {
                            throw FormatError("M2PG sub-texture LOD extends past texel data");
                        }
                        LodLevel lod;
                        lod.data.assign(blob.begin() + pos, blob.begin() + pos + bytes);
                        sub.lods_.push_back(std::move(lod));
                        pos += bytes;
                    }
                }
                textures.push_back(std::move(sub));
            }
            continue; // the page-canvas texture itself isn't drawable
        }

        textures.push_back(std::move(tex));
    }
    return textures;
}

std::vector<Texture> Texture::loadAllFromFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw FormatError("could not open file: " + path.string());
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
    return loadAll(bytes.data(), bytes.size());
}

Texture Texture::load(const uint8_t* data, size_t size) {
    return loadAll(data, size).front();
}

Texture Texture::loadFromFile(const std::filesystem::path& path) {
    return loadAllFromFile(path).front();
}

uint32_t Texture::lodWidth(size_t lodIndex) const {
    // dim(lod) = min << (numLOD - lod - 1) — M2TXHeader_GetLODDim.
    return uint32_t(header_.minXSize) << (header_.numLOD - lodIndex - 1);
}

uint32_t Texture::lodHeight(size_t lodIndex) const {
    return uint32_t(header_.minYSize) << (header_.numLOD - lodIndex - 1);
}

std::vector<Rgba8> Texture::decodeLodToRgba(size_t lodIndex) const {
    if (lodIndex >= lods_.size()) {
        throw FormatError("decodeLodToRgba: LOD index out of range");
    }
    if (header_.numLOD == 0 || lodIndex >= header_.numLOD) {
        throw FormatError("decodeLodToRgba: LOD index exceeds header NumLOD");
    }

    const uint32_t width = lodWidth(lodIndex);
    const uint32_t height = lodHeight(lodIndex);
    const size_t pixelCount = size_t(width) * height;
    std::vector<Rgba8> out(pixelCount);

    const auto& raw = lods_[lodIndex].data;
    BitReader bits(raw.data(), raw.size());

    if (!isCompressed()) {
        for (size_t i = 0; i < pixelCount; ++i) {
            out[i] = decodeTexel(bits, header_.texFormat, pip_, hasPip_);
        }
        if (bits.overran()) {
            throw FormatError("decodeLodToRgba: texel data ended early (uncompressed)");
        }
        return out;
    }

    // RLE-compressed: control byte selects one of the 4 DCI texel formats
    // (top 2 bits). Transparent formats (M2CI_IsTrans) encode a skip run of
    // (control & 0x3F) + 1 pixels rendered as that format's expansion color
    // constant; others encode (control & 0x1F) + 1 pixels, where bit 0x20
    // ("isString") distinguishes count literal texels from a single texel
    // repeated count times. Port of the compressed branch of the reference
    // LOD walker in M2TXLibrary.c.
    if (!hasDci_) {
        throw FormatError("compressed texture has no DCI chunk");
    }
    size_t i = 0;
    while (i < pixelCount) {
        uint32_t control = bits.read(8);
        if (bits.overran()) {
            throw FormatError("decodeLodToRgba: texel data ended early (compressed)");
        }
        uint8_t fmtIndex = uint8_t((control & 0xC0) >> 6);
        uint16_t fmt = dci_.texelFormat[fmtIndex];

        if (fmt & TexelFlags::IsTrans) {
            size_t count = (control & 0x3F) + 1;
            Rgba8 c = decodeM2TXColor(dci_.txExpColorConst[fmtIndex]);
            c.a = 0; // transparent run — expansion constant carries RGB only
            for (size_t k = 0; k < count && i < pixelCount; ++k, ++i) {
                out[i] = c;
            }
        } else {
            size_t count = (control & 0x1F) + 1;
            bool isString = (control & 0x20) != 0;
            if (isString) {
                for (size_t k = 0; k < count && i < pixelCount; ++k, ++i) {
                    out[i] = decodeTexel(bits, fmt, pip_, hasPip_);
                }
            } else {
                Rgba8 c = decodeTexel(bits, fmt, pip_, hasPip_);
                for (size_t k = 0; k < count && i < pixelCount; ++k, ++i) {
                    out[i] = c;
                }
            }
        }
    }
    return out;
}

} // namespace m2texture
