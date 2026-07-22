#include "m2cel/Cel.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>

#include "m2core/BitReader.h"
#include "m2core/ByteStream.h"
#include "m2core/Error.h"
#include "m2core/Iff.h"

namespace m2cel {

using m2core::BitReader;
using m2core::ByteReader;
using m2core::FormatError;
using m2texture::Rgba8;

namespace {
constexpr uint32_t ID_CCB = m2core::makeId('C', 'C', 'B', ' ');
constexpr uint32_t ID_PDAT = m2core::makeId('P', 'D', 'A', 'T');
constexpr uint32_t ID_PLUT = m2core::makeId('P', 'L', 'U', 'T');

// Packed-row packet codes (2 high bits of each control byte).
constexpr uint8_t PACK_EOL = 0;
constexpr uint8_t PACK_LITERAL = 1;
constexpr uint8_t PACK_TRANSPARENT = 2;
constexpr uint8_t PACK_REPEAT = 3;
} // namespace

uint8_t CcbHeader::bitsPerPixel() const {
    switch (bppCode()) {
        case 1: return 1;
        case 2: return 2;
        case 3: return 4;
        case 4: return 6;
        case 5: return 8;
        case 6: return 16;
        default:
            throw FormatError("CCB PRE0 has invalid bits-per-pixel code " +
                               std::to_string(bppCode()));
    }
}

Rgba8 expandPlutEntry(uint16_t entry) {
    auto expand5 = [](uint16_t v) { return uint8_t((v << 3) | (v >> 2)); };
    Rgba8 c;
    c.r = expand5((entry >> 10) & 0x1F);
    c.g = expand5((entry >> 5) & 0x1F);
    c.b = expand5(entry & 0x1F);
    c.a = 255;
    return c;
}

Cel Cel::makeRaw(const uint8_t* data, size_t size) {
    constexpr uint32_t kWidth = 320;
    if (size < kWidth * 2 || (size % (kWidth * 2)) != 0) {
        throw FormatError("not a 3DO cel file: no 'CCB ' chunk and size "
                           "doesn't match a raw 320-wide 16bpp image");
    }
    Cel cel;
    cel.ccb_.width = kWidth;
    cel.ccb_.height = uint32_t(size / (kWidth * 2));
    cel.ccb_.pre0 = 0x10 | 6; // uncoded (bit 4), bpp code 6 = 16bpp
    cel.ccb_.flags = 0;        // not packed

    // These headerless dumps are M1 VRAM captures, which are stored
    // LRForm: rows come in vertical pairs with each pair-row holding
    // alternating (x,y) / (x,y+1) 16-bit words. De-interleave to plain
    // top-to-bottom rows here so the generic decoder can stay linear.
    // (Verified: Alone in the Dark's tatnoir.cel renders the Infogrames
    // logo only under this layout — linear reads as shifted lines.)
    const uint32_t height = cel.ccb_.height;
    cel.pdat_.resize(size);
    const uint32_t pairRows = height / 2;
    for (uint32_t pr = 0; pr < pairRows; ++pr) {
        const uint8_t* src = data + size_t(pr) * kWidth * 4;
        uint8_t* topRow = cel.pdat_.data() + size_t(pr * 2) * kWidth * 2;
        uint8_t* botRow = topRow + size_t(kWidth) * 2;
        for (uint32_t x = 0; x < kWidth; ++x) {
            topRow[x * 2] = src[x * 4];
            topRow[x * 2 + 1] = src[x * 4 + 1];
            botRow[x * 2] = src[x * 4 + 2];
            botRow[x * 2 + 1] = src[x * 4 + 3];
        }
    }
    if (height % 2) { // odd trailing row: copy as-is
        std::memcpy(cel.pdat_.data() + size_t(height - 1) * kWidth * 2,
                     data + size_t(height - 1) * kWidth * 2, kWidth * 2);
    }
    return cel;
}

Cel Cel::load(const uint8_t* data, size_t size) {
    // A .cel file is a flat chunk sequence (no FORM wrapper, unlike UTF).
    // Headerless raw framebuffers (no CCB magic) fall through to makeRaw.
    if (size >= 4) {
        uint32_t magic =
            (uint32_t(data[0]) << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
        if (magic != ID_CCB && magic != ID_PDAT && magic != ID_PLUT) {
            return makeRaw(data, size);
        }
    }

    ByteReader r(data, size);
    Cel cel;
    bool sawCcb = false;

    while (r.remaining() >= 8) {
        size_t chunkStart = r.position();
        uint32_t id = r.readU32BE();
        uint32_t chunkSize = r.readU32BE();
        if (chunkSize < 8 || chunkSize - 8 > r.remaining()) {
            // Trailing data / padding / a second packed structure past the
            // first cel (common in .DAT bundles like Street Fighter's
            // QSOUND.DAT and syukyakuDEMO.dat). If we already decoded a
            // complete first cel, stop cleanly and show it; otherwise the
            // file genuinely isn't a cel.
            if (sawCcb && !cel.pdat_.empty()) {
                break;
            }
            throw FormatError("cel chunk '" + m2core::idToString(id) +
                               "' has bad size " + std::to_string(chunkSize));
        }
        // chunkSize includes the 8-byte id+size header itself (verified:
        // Message.cel's CCB chunk says 0x50 = the full 80 bytes).
        uint32_t bodySize = chunkSize - 8;

        if (id == ID_CCB) {
            // A second CCB begins another cel — keep just the first drawable
            // one for display.
            if (sawCcb && !cel.pdat_.empty()) {
                r.seek(chunkStart);
                break;
            }
            if (bodySize < 72) {
                throw FormatError("CCB chunk body smaller than 72 bytes");
            }
            size_t bodyStart = r.position();
            cel.ccb_.version = r.readU32BE();
            cel.ccb_.flags = r.readU32BE();
            cel.ccb_.nextPtr = r.readU32BE();
            cel.ccb_.sourcePtr = r.readU32BE();
            cel.ccb_.plutPtr = r.readU32BE();
            cel.ccb_.xPos = int32_t(r.readU32BE());
            cel.ccb_.yPos = int32_t(r.readU32BE());
            cel.ccb_.hdx = int32_t(r.readU32BE());
            cel.ccb_.hdy = int32_t(r.readU32BE());
            cel.ccb_.vdx = int32_t(r.readU32BE());
            cel.ccb_.vdy = int32_t(r.readU32BE());
            cel.ccb_.hddx = int32_t(r.readU32BE());
            cel.ccb_.hddy = int32_t(r.readU32BE());
            cel.ccb_.pixc = r.readU32BE();
            cel.ccb_.pre0 = r.readU32BE();
            cel.ccb_.pre1 = r.readU32BE();
            cel.ccb_.width = r.readU32BE();
            cel.ccb_.height = r.readU32BE();
            r.seek(bodyStart + bodySize);
            sawCcb = true;
        } else if (id == ID_PDAT) {
            cel.pdat_ = r.readBytes(bodySize);
        } else if (id == ID_PLUT) {
            if (bodySize < 4) {
                throw FormatError("PLUT chunk body smaller than 4 bytes");
            }
            uint32_t numEntries = r.readU32BE();
            if (numEntries > (bodySize - 4) / 2) {
                throw FormatError("PLUT entry count exceeds chunk size");
            }
            cel.plut_.resize(numEntries);
            for (auto& e : cel.plut_) e = r.readU16BE();
            r.seek(r.position() + (bodySize - 4 - numEntries * 2));
        } else {
            r.skip(bodySize); // unknown chunk (e.g. XTRA, ANIM) — preserve-skip
        }
    }

    if (!sawCcb) {
        throw FormatError("not a 3DO cel file: no 'CCB ' chunk found");
    }
    if (cel.pdat_.empty()) {
        throw FormatError("cel file has no PDAT pixel-data chunk");
    }
    return cel;
}

Cel Cel::loadFromFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw FormatError("could not open file: " + path.string());
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
    return load(bytes.data(), bytes.size());
}

std::vector<Rgba8> Cel::decodeToRgba() const {
    return decodeCelFrame(ccb_, pdat_, plut_);
}

std::vector<Rgba8> decodeCelFrame(const CcbHeader& ccb,
                                   const std::vector<uint8_t>& pdat,
                                   const std::vector<uint16_t>& plut) {
    const uint8_t bpp = ccb.bitsPerPixel();
    const uint32_t width = ccb.width;
    const uint32_t height = ccb.height;
    const bool uncoded = ccb.isUncoded();
    std::vector<Rgba8> out(size_t(width) * height); // zero-init = transparent

    if (!uncoded && plut.empty()) {
        throw FormatError("coded cel has no PLUT palette chunk");
    }

    // Maps one raw texel value to RGBA. Coded cels: <=4bpp values index the
    // PLUT directly; 6/8/16bpp coded use the 5 low bits (upper bits carry
    // SSB/AMV shading applied by the cel engine at render time — ignored
    // for still decode). Out-of-range indices wrap like the 32-entry
    // hardware PLUT RAM does rather than erroring.
    auto toColor = [&](uint32_t raw) -> Rgba8 {
        if (uncoded) {
            if (bpp == 16) {
                return expandPlutEntry(uint16_t(raw)); // direct 5-5-5
            }
            Rgba8 c; // uncoded sub-16bpp: grayscale fallback (rare)
            c.r = c.g = c.b = uint8_t(raw);
            c.a = 255;
            return c;
        }
        uint32_t index = (bpp >= 6) ? (raw & 0x1F) : raw;
        if (index >= plut.size()) {
            index %= plut.size();
        }
        return expandPlutEntry(plut[index]);
    };

    if (!ccb.isPacked()) {
        // Unpacked: each row is an MSB-first bitstream padded to a 32-bit
        // word boundary (CelLib.cpp: rowWords = (x_res*bpp+31)>>5).
        size_t rowBytes = ((size_t(width) * bpp + 31) / 32) * 4;
        for (uint32_t y = 0; y < height; ++y) {
            size_t rowStart = y * rowBytes;
            if (rowStart >= pdat.size()) {
                throw FormatError("unpacked cel: pixel data ended early");
            }
            BitReader bits(pdat.data() + rowStart,
                            std::min(rowBytes, pdat.size() - rowStart));
            for (uint32_t x = 0; x < width; ++x) {
                out[size_t(y) * width + x] = toColor(bits.read(bpp));
            }
        }
        return out;
    }

    // Packed (RLE) rows. Each row leads with an offset to the next row in
    // 32-bit words minus 2 — one byte for sub-8bpp cels, two bytes for
    // 8/16bpp (CelLib.cpp CelToRaw). After the offset the row is one
    // continuous MSB-first bitstream: 8-bit packet controls (2-bit code +
    // 6-bit count-1) interleaved with bpp-sized pixel values, NOT
    // byte-aligned for sub-byte depths.
    const size_t offsetBytes = (bpp >= 8) ? 2 : 1;

    // Some cels prefix the packed rows with a 4-byte preamble and some
    // don't (verified: StarBlade's DANGER.cel needs +4 — its 10 rows then
    // land exactly on the PLUT boundary — while Street Fighter's
    // titleDEMO.dat starts at +0). Rather than guess, walk the row-offset
    // chain for each candidate start and keep the one that consumes the
    // PDAT cleanly for `height` rows. A chain that overruns the buffer or
    // stops far short is rejected.
    auto chainScore = [&](size_t start) -> long long {
        size_t pos = start;
        for (uint32_t y = 0; y < height; ++y) {
            if (pos + offsetBytes > pdat.size()) {
                return -1; // ran out of rows early
            }
            uint32_t words = (offsetBytes == 2)
                                  ? (uint32_t(pdat[pos]) << 8) | pdat[pos + 1]
                                  : pdat[pos];
            pos += (size_t(words) + 2) * 4;
            if (pos > pdat.size()) {
                return -1; // row claims data past the chunk
            }
        }
        // Prefer the start whose chain ends closest to the end of PDAT.
        return static_cast<long long>(pdat.size() - pos);
    };

    size_t dataStart = 0;
    {
        long long best = -1;
        for (size_t candidate : {size_t(0), size_t(4)}) {
            long long score = chainScore(candidate);
            if (score >= 0 && (best < 0 || score < best)) {
                best = score;
                dataStart = candidate;
            }
        }
        // If neither validates, fall back to 0 and decode defensively.
    }

    size_t rowStart = dataStart;
    for (uint32_t y = 0; y < height; ++y) {
        if (rowStart + offsetBytes > pdat.size()) {
            // Data ran out early — render what we have (remaining rows stay
            // transparent) rather than failing the whole cel. Some game
            // cels (e.g. Policenauts .anm frames) legitimately end their
            // packed data before the nominal last row.
            break;
        }
        uint32_t offsetWords = (offsetBytes == 2)
                                    ? (uint32_t(pdat[rowStart]) << 8) | pdat[rowStart + 1]
                                    : pdat[rowStart];
        size_t nextRowStart = rowStart + (size_t(offsetWords) + 2) * 4;
        size_t streamEnd = std::min(nextRowStart, pdat.size());

        BitReader bits(pdat.data() + rowStart + offsetBytes,
                        streamEnd - rowStart - offsetBytes);
        uint32_t x = 0;
        while (x < width && !bits.overran()) {
            uint32_t control = bits.read(8);
            if (bits.overran()) {
                break; // row data exhausted — remaining pixels stay transparent
            }
            uint8_t code = uint8_t(control >> 6);
            uint32_t count = (control & 0x3F) + 1;
            if (code == 0) {
                break; // end of row
            }
            if (count > width - x) {
                count = width - x;
            }
            switch (code) {
                case 1: // literal
                    for (uint32_t i = 0; i < count; ++i, ++x) {
                        out[size_t(y) * width + x] = toColor(bits.read(bpp));
                    }
                    break;
                case 2: // transparent run
                    x += count;
                    break;
                case 3: { // repeat one value
                    Rgba8 c = toColor(bits.read(bpp));
                    for (uint32_t i = 0; i < count; ++i, ++x) {
                        out[size_t(y) * width + x] = c;
                    }
                    break;
                }
                default:
                    break;
            }
        }
        rowStart = nextRowStart;
    }
    return out;
}

} // namespace m2cel
