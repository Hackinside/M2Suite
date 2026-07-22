#include "m2texture/TextureEncoder.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#include "m2texture/Texture.h"

namespace m2texture {

namespace {

// MSB-first bit writer mirroring the decoder's BitReader.
class BitWriter {
public:
    void write(uint32_t value, uint8_t bits) {
        for (int i = int(bits) - 1; i >= 0; --i) {
            uint8_t bit = uint8_t((value >> i) & 1u);
            if (bitPos_ == 0) {
                bytes_.push_back(0);
            }
            if (bit) {
                bytes_.back() |= uint8_t(0x80u >> bitPos_);
            }
            bitPos_ = uint8_t((bitPos_ + 1) & 7);
        }
    }
    // Pads the current partial byte with zeros.
    void flush() { bitPos_ = 0; }
    const std::vector<uint8_t>& bytes() const { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
    uint8_t bitPos_ = 0;
};

void putU32BE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v >> 24));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

void putU16BE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

// Reduces an 8-bit component to `depth` bits, rounding to nearest so that
// full-scale stays full-scale (255 -> all-ones) — the inverse of the
// decoder's expandComponent().
uint32_t reduceComponent(uint8_t value, uint8_t depth) {
    if (depth >= 8) {
        return value;
    }
    uint32_t maxOut = (1u << depth) - 1u;
    return (uint32_t(value) * maxOut + 127u) / 255u;
}

void appendChunk(std::vector<uint8_t>& out, uint32_t id, const std::vector<uint8_t>& body) {
    // IFF chunk: id(4) size(4) body, padded to a 4-byte boundary. Per
    // IffForm::parse, the stored size counts the BODY only (the reader is
    // already past id+size when it consumes `chunkSize` bytes) — note this
    // differs from the 3DO cel format, where the size includes the header.
    putU32BE(out, id);
    putU32BE(out, uint32_t(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    while ((out.size() & 3u) != 0) {
        out.push_back(0);
    }
}

constexpr uint32_t kIdForm = ('F' << 24) | ('O' << 16) | ('R' << 8) | 'M';
constexpr uint32_t kIdTxtr = ('T' << 24) | ('X' << 16) | ('T' << 8) | 'R';
constexpr uint32_t kIdM2tx = ('M' << 24) | ('2' << 16) | ('T' << 8) | 'X';
constexpr uint32_t kIdM2td = ('M' << 24) | ('2' << 16) | ('T' << 8) | 'D';

} // namespace

EncodeOptions defaultEncodeOptions(const Rgba8* pixels, uint32_t width, uint32_t height) {
    EncodeOptions o;
    o.colorDepth = 8;
    o.alphaDepth = 0;
    size_t count = size_t(width) * height;
    for (size_t i = 0; i < count; ++i) {
        if (pixels[i].a != 255) {
            o.alphaDepth = 7; // M2 stores at most 7 bits of alpha
            break;
        }
    }
    return o;
}

std::vector<uint8_t> encodeUtf(const Rgba8* pixels, uint32_t width, uint32_t height,
                                const EncodeOptions& options) {
    if (!pixels || width == 0 || height == 0) {
        throw std::runtime_error("encodeUtf: empty image");
    }
    if (width > 0xFFFF || height > 0xFFFF) {
        throw std::runtime_error("encodeUtf: image larger than the 16-bit M2 dimension fields");
    }
    uint8_t cDepth = std::clamp<uint8_t>(options.colorDepth, 1, 8);
    uint8_t aDepth = std::min<uint8_t>(options.alphaDepth, 7);

    // TexFormat: literal RGB colour, optional alpha. No SSB, no PIP.
    uint16_t texFormat = uint16_t(TexelFlags::IsLiteral | TexelFlags::HasColor | cDepth);
    if (aDepth > 0) {
        texFormat = uint16_t(texFormat | TexelFlags::HasAlpha | (uint16_t(aDepth) << 4));
    }

    // Texel bitstream, row-major top-to-bottom, matching decodeTexel's
    // read order: [alpha][R][G][B].
    BitWriter bw;
    size_t count = size_t(width) * height;
    for (size_t i = 0; i < count; ++i) {
        const Rgba8& p = pixels[i];
        if (aDepth > 0) {
            bw.write(reduceComponent(p.a, aDepth), aDepth);
        }
        bw.write(reduceComponent(p.r, cDepth), cDepth);
        bw.write(reduceComponent(p.g, cDepth), cDepth);
        bw.write(reduceComponent(p.b, cDepth), cDepth);
    }
    bw.flush();

    // M2TX header chunk (16 bytes).
    std::vector<uint8_t> m2tx;
    putU32BE(m2tx, 0);          // reserved
    putU32BE(m2tx, 0);          // flags: uncompressed, no PIP
    putU16BE(m2tx, uint16_t(width));   // MinXSize (== width for NumLOD 1)
    putU16BE(m2tx, uint16_t(height));  // MinYSize
    putU16BE(m2tx, texFormat);
    m2tx.push_back(1);          // NumLOD
    m2tx.push_back(0);          // reserved2

    // M2TD carries its own LOD table before the texel bytes:
    // NumLOD(2) Reserved(2) Offset[NumLOD](4 each), offsets measured from
    // the start of the chunk body (see parseM2TD).
    std::vector<uint8_t> m2td;
    putU16BE(m2td, 1); // numLOD
    putU16BE(m2td, 0); // reserved
    putU32BE(m2td, 8); // offset of LOD 0 = past this 8-byte table
    m2td.insert(m2td.end(), bw.bytes().begin(), bw.bytes().end());

    std::vector<uint8_t> body;
    putU32BE(body, kIdTxtr); // FORM type
    appendChunk(body, kIdM2tx, m2tx);
    appendChunk(body, kIdM2td, m2td);

    // FORM size is measured from just after the size field, i.e. it covers
    // the 4-byte form type plus every chunk (IffForm::parse computes
    // formEnd = position_after_size + formSize).
    std::vector<uint8_t> out;
    putU32BE(out, kIdForm);
    putU32BE(out, uint32_t(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

void writeUtfFile(const std::filesystem::path& path, const Rgba8* pixels, uint32_t width,
                   uint32_t height, const EncodeOptions& options) {
    std::vector<uint8_t> bytes = encodeUtf(pixels, width, height, options);
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("could not write UTF file: " + path.string());
    }
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

} // namespace m2texture
