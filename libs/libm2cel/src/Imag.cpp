#include "m2cel/Imag.h"

#include <fstream>
#include <string>

#include "m2cel/Cel.h" // CcbHeader, CCB_PACKED, decodeCelFrame (shared packed decoder)
#include "m2core/ByteStream.h"
#include "m2core/Error.h"
#include "m2core/Iff.h"

namespace m2cel {

using m2core::ByteReader;
using m2core::FormatError;
using m2core::NotImplementedError;
using m2texture::Rgba8;

namespace {
constexpr uint32_t ID_IMAG = m2core::makeId('I', 'M', 'A', 'G');
constexpr uint32_t ID_PDAT = m2core::makeId('P', 'D', 'A', 'T');

Rgba8 expand555(uint16_t v) {
    auto expand5 = [](uint16_t c) { return uint8_t((c << 3) | (c >> 2)); };
    Rgba8 out;
    out.r = expand5((v >> 10) & 0x1F);
    out.g = expand5((v >> 5) & 0x1F);
    out.b = expand5(v & 0x1F);
    out.a = 255;
    return out;
}
} // namespace

Imag Imag::load(const uint8_t* data, size_t size) {
    ByteReader r(data, size);
    Imag img;
    bool sawImag = false;

    while (r.remaining() >= 8) {
        uint32_t id = r.readU32BE();
        uint32_t chunkSize = r.readU32BE();
        if (chunkSize < 8 || chunkSize - 8 > r.remaining()) {
            throw FormatError("IMAG chunk '" + m2core::idToString(id) +
                               "' has bad size " + std::to_string(chunkSize));
        }
        uint32_t bodySize = chunkSize - 8;
        size_t bodyStart = r.position();

        if (id == ID_IMAG) {
            if (bodySize < 20) {
                throw FormatError("IMAG control chunk body smaller than 20 bytes");
            }
            img.width = r.readU32BE();
            img.height = r.readU32BE();
            img.bytesPerRow = r.readU32BE();
            img.bitsPerPixel = r.readU8();
            img.numComponents = r.readU8();
            img.numPlanes = r.readU8();
            img.colorSpace = r.readU8();
            img.compType = r.readU8();
            img.hvFormat = r.readU8();
            img.pixelOrder = r.readU8();
            img.version = r.readU8();
            sawImag = true;
        } else if (id == ID_PDAT) {
            img.pdat_ = r.readBytes(bodySize);
        }
        size_t next = bodyStart + bodySize;
        next = (next + 3) & ~size_t(3);
        if (next > r.size()) {
            next = r.size();
        }
        r.seek(next);
    }

    if (!sawImag) {
        throw FormatError("not a 3DO IMAG file: no 'IMAG' chunk found");
    }
    if (img.pdat_.empty()) {
        throw FormatError("IMAG file has no PDAT chunk");
    }
    return img;
}

Imag Imag::loadFromFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw FormatError("could not open file: " + path.string());
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
    return load(bytes.data(), bytes.size());
}

std::vector<Rgba8> Imag::decodeToRgba() const {
    if (bitsPerPixel != 16) {
        throw NotImplementedError("IMAG decode: " + std::to_string(bitsPerPixel) +
                                   "bpp not supported (only 16-bit 0555)");
    }
    // comptype 1 nominally means "Cel bit packed" (form3do.h), but real
    // files lie: Yu Yu Hakusho's STOP.IMAG and Live! 3DO Magazine's
    // back.IMG both flag comptype 1 while carrying a full-size
    // *uncompressed* buffer (PDAT == width*height*2 exactly). Trust the
    // data over the flag — only take the packed path when the payload is
    // genuinely smaller than an uncompressed image would be.
    const size_t uncompressedBytes = size_t(width) * height * 2;
    const bool actuallyPacked = (compType != 0) && (pdat_.size() < uncompressedBytes);

    if (actuallyPacked) {
        CcbHeader ccb;
        ccb.width = width;
        ccb.height = height;
        ccb.pre0 = 0x10 | 6;         // uncoded (bit 4), bpp code 6 = 16bpp
        ccb.flags = CCB_PACKED;      // packed (RLE)
        return decodeCelFrame(ccb, pdat_, {});
    }
    if (compType != 0 && compType != 1) {
        throw NotImplementedError("IMAG decode: compression type " +
                                   std::to_string(compType) + " not supported");
    }

    std::vector<Rgba8> out(size_t(width) * height);
    ByteReader r(pdat_);

    if (pixelOrder == 0) {
        // Linear rows.
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                out[size_t(y) * width + x] = expand555(r.readU16BE());
            }
        }
        return out;
    }

    // LRForm (M1 VRAM layout): rows are stored in vertically adjacent
    // pairs, words alternating between the upper and lower row:
    // (x, y), (x, y+1), (x+1, y), (x+1, y+1), ...
    uint32_t pairRows = (height + 1) / 2;
    for (uint32_t pr = 0; pr < pairRows; ++pr) {
        uint32_t y0 = pr * 2;
        uint32_t y1 = y0 + 1;
        for (uint32_t x = 0; x < width; ++x) {
            uint16_t top = r.readU16BE();
            uint16_t bottom = r.readU16BE();
            out[size_t(y0) * width + x] = expand555(top);
            if (y1 < height) {
                out[size_t(y1) * width + x] = expand555(bottom);
            }
        }
    }
    return out;
}

} // namespace m2cel
