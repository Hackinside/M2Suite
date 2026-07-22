#include "m2audio/Aiff.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include "m2audio/Sdx2.h"

#include "m2core/ByteStream.h"
#include "m2core/Error.h"
#include "m2core/Iff.h"

namespace m2audio {

using m2core::ByteReader;
using m2core::ByteWriter;
using m2core::FormatError;
using m2core::NotImplementedError;

namespace {
constexpr uint32_t ID_AIFF = m2core::makeId('A', 'I', 'F', 'F');
constexpr uint32_t ID_AIFC = m2core::makeId('A', 'I', 'F', 'C');
constexpr uint32_t ID_COMM = m2core::makeId('C', 'O', 'M', 'M');
constexpr uint32_t ID_SSND = m2core::makeId('S', 'S', 'N', 'D');
constexpr uint32_t ID_NONE = m2core::makeId('N', 'O', 'N', 'E');

// AIFF sample rates are stored as 80-bit IEEE 754 extended-precision:
// 1 sign bit, 15-bit biased exponent, 64-bit mantissa (explicit leading
// bit). value = mantissa * 2^(exponent - 16383 - 63).
double readExtended80(ByteReader& r) {
    uint16_t se = r.readU16BE();
    uint64_t mantissa = (uint64_t(r.readU32BE()) << 32) | r.readU32BE();
    int sign = (se & 0x8000) ? -1 : 1;
    int exponent = se & 0x7FFF;
    if (exponent == 0 && mantissa == 0) {
        return 0.0;
    }
    double value = std::ldexp(double(mantissa), exponent - 16383 - 63);
    return sign * value;
}
constexpr uint64_t kSector = 2048; // catalogue entries align to CD sectors

bool isFormAiff(const uint8_t* p) {
    return p[0] == 'F' && p[1] == 'O' && p[2] == 'R' && p[3] == 'M' && p[8] == 'A' &&
           p[9] == 'I' && p[10] == 'F' && (p[11] == 'F' || p[11] == 'C');
}
uint32_t beU32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) |
           uint32_t(p[3]);
}
} // namespace

std::vector<AiffCatalogueEntry> scanAiffCatalogue(const uint8_t* data, size_t size) {
    std::vector<AiffCatalogueEntry> out;
    if (size < 12) {
        return out;
    }
    for (uint64_t off = 0; off + 12 <= size; off += kSector) {
        const uint8_t* p = data + off;
        if (!isFormAiff(p)) {
            continue;
        }
        uint64_t formSize = uint64_t(beU32(p + 4)) + 8;
        if (formSize < 12 || off + formSize > size) {
            continue; // truncated or a coincidence
        }
        out.push_back({off, formSize});
    }
    return out;
}

bool looksLikeAiffCatalogue(const uint8_t* header, size_t headerSize, uint64_t fileSize) {
    if (headerSize < 12 || !isFormAiff(header)) {
        return false;
    }
    uint64_t formSize = uint64_t(beU32(header + 4)) + 8;
    // A single sound accounts for its whole file (bar a little padding). A
    // catalogue leaves sectors of it unexplained.
    return fileSize > formSize + kSector;
}

Aiff Aiff::load(const uint8_t* data, size_t size) {
    // Some discs bundle several sounds in a 3DO 'RSRC' resource file (an
    // 'RTBL' table of {type, id, offset, size} records) rather than a bare
    // FORM AIFF — e.g. Road Rash's Rash.AIFF holds 19 AIFF resources.
    // Transparently unwrap to the first AIFF resource so it plays; the rest
    // are reachable by extracting the RSRC.
    if (size >= 0x40 && data[0] == 'R' && data[1] == 'S' && data[2] == 'R' && data[3] == 'C') {
        // RTBL usually follows the 0x30-byte RSRC header.
        for (size_t p = 0x30; p + 16 <= size; p += 4) {
            if (data[p] == 'R' && data[p + 1] == 'T' && data[p + 2] == 'B' &&
                data[p + 3] == 'L') {
                uint32_t count = (uint32_t(data[p + 8]) << 24) | (data[p + 9] << 16) |
                                 (data[p + 10] << 8) | data[p + 11];
                size_t entry = p + 16; // records begin after tag/size/count/pad
                for (uint32_t i = 0; i < count && entry + 16 <= size; ++i, entry += 32) {
                    if (data[entry] == 'A' && data[entry + 1] == 'I' &&
                        data[entry + 2] == 'F' && data[entry + 3] == 'F') {
                        uint32_t off = (uint32_t(data[entry + 8]) << 24) |
                                       (data[entry + 9] << 16) | (data[entry + 10] << 8) |
                                       data[entry + 11];
                        uint32_t rsize = (uint32_t(data[entry + 12]) << 24) |
                                         (data[entry + 13] << 16) | (data[entry + 14] << 8) |
                                         data[entry + 15];
                        if (off + 4 <= size && data[off] == 'F' && data[off + 1] == 'O') {
                            size_t avail = std::min<size_t>(rsize, size - off);
                            return load(data + off, avail);
                        }
                    }
                }
                break;
            }
        }
    }

    ByteReader reader(data, size);
    // Classic EA-IFF-85: 2-byte (even) chunk alignment, unlike M2's
    // 4-byte-aligned IFF folio files.
    m2core::IffForm form = m2core::IffForm::parse(reader, 2);

    Aiff aiff;
    if (form.formType() == ID_AIFC) {
        aiff.isAifc_ = true;
    } else if (form.formType() != ID_AIFF) {
        throw FormatError("not an AIFF/AIFC file: FORM type is '" +
                           m2core::idToString(form.formType()) + "'");
    }

    const auto* comm = form.find(ID_COMM);
    if (!comm) {
        throw FormatError("AIFF file has no COMM chunk");
    }
    if (comm->size() < 18) {
        throw FormatError("AIFF COMM chunk smaller than 18 bytes");
    }
    {
        ByteReader r(*comm);
        aiff.channels_ = r.readU16BE();
        aiff.sampleFrames_ = r.readU32BE();
        aiff.bitsPerSample_ = r.readU16BE();
        aiff.sampleRate_ = readExtended80(r);
        if (aiff.isAifc_ && r.remaining() >= 4) {
            aiff.compressionFourcc_ = r.readU32BE();
            aiff.compressionType_ = m2core::idToString(aiff.compressionFourcc_);
        }
    }
    if (aiff.channels_ == 0) {
        throw FormatError("AIFF COMM chunk has zero channels");
    }

    const auto* ssnd = form.find(ID_SSND);
    if (!ssnd) {
        throw FormatError("AIFF file has no SSND chunk");
    }
    if (ssnd->size() < 8) {
        throw FormatError("AIFF SSND chunk smaller than 8 bytes");
    }
    {
        ByteReader r(*ssnd);
        uint32_t offset = r.readU32BE();
        r.skip(4); // blockSize — informational for block-aligned playback
        if (8u + offset > ssnd->size()) {
            throw FormatError("AIFF SSND offset extends past chunk");
        }
        r.skip(offset);
        aiff.sampleData_ = r.readBytes(r.remaining());
    }

    return aiff;
}

Aiff Aiff::loadFromFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw FormatError("could not open file: " + path.string());
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
    return load(bytes.data(), bytes.size());
}

std::vector<int16_t> Aiff::decodePcm16() const {
    // 3DO 2:1 codecs: one byte per sample, channel-interleaved.
    if (compressionFourcc_ == m2core::makeId('S', 'D', 'X', '2') ||
        compressionFourcc_ == m2core::makeId('S', 'Q', 'S', '2')) {
        return decodeSdx2(sampleData_.data(), sampleData_.size(), channels_);
    }
    if (compressionFourcc_ == m2core::makeId('C', 'B', 'D', '2')) {
        return decodeCbd2(sampleData_.data(), sampleData_.size(), channels_);
    }
    // ADP4 = 4-bit IMA/DVI ADPCM (audio folio sub_decode_adp4.ins), 2
    // samples per byte. Yu Yu Hakusho's SOUND/*.sc use it.
    if (compressionFourcc_ == m2core::makeId('A', 'D', 'P', '4')) {
        std::vector<int16_t> pcm =
            decodeAdp4(sampleData_.data(), sampleData_.size(), channels_);
        // Trim to the frame count the header declares (the last byte can
        // carry a padding nibble).
        size_t declared = size_t(sampleFrames_) * channels_;
        if (declared > 0 && pcm.size() > declared) {
            pcm.resize(declared);
        }
        return pcm;
    }
    if (compressionFourcc_ != ID_NONE) {
        throw NotImplementedError("AIFC compression '" + compressionType_ +
                                   "' not supported (NONE/SDX2/SQS2/CBD2/ADP4 only)");
    }

    size_t totalSamples = size_t(sampleFrames_) * channels_;
    std::vector<int16_t> out;
    out.reserve(totalSamples);

    ByteReader r(sampleData_);
    if (bitsPerSample_ == 16) {
        size_t available = sampleData_.size() / 2;
        size_t n = totalSamples < available ? totalSamples : available;
        for (size_t i = 0; i < n; ++i) {
            out.push_back(int16_t(r.readU16BE()));
        }
    } else if (bitsPerSample_ == 8) {
        // AIFF 8-bit is signed (unlike WAV's unsigned 8-bit).
        size_t available = sampleData_.size();
        size_t n = totalSamples < available ? totalSamples : available;
        for (size_t i = 0; i < n; ++i) {
            out.push_back(int16_t(int8_t(r.readU8())) << 8);
        }
    } else {
        throw NotImplementedError("AIFF decode: " + std::to_string(bitsPerSample_) +
                                   "-bit samples not supported (only 8/16-bit PCM)");
    }
    return out;
}

std::vector<uint8_t> Aiff::toWavBytes() const {
    std::vector<int16_t> pcm = decodePcm16();
    uint32_t sampleRate = uint32_t(sampleRate_ + 0.5);
    uint32_t dataBytes = uint32_t(pcm.size() * 2);
    uint16_t blockAlign = uint16_t(channels_ * 2);

    // WAV/RIFF is little-endian throughout — written byte-by-byte here
    // rather than adding a little-endian writer for one function.
    ByteWriter w;
    auto u16le = [&w](uint16_t v) {
        w.writeU8(uint8_t(v & 0xFF));
        w.writeU8(uint8_t(v >> 8));
    };
    auto u32le = [&w](uint32_t v) {
        w.writeU8(uint8_t(v & 0xFF));
        w.writeU8(uint8_t((v >> 8) & 0xFF));
        w.writeU8(uint8_t((v >> 16) & 0xFF));
        w.writeU8(uint8_t((v >> 24) & 0xFF));
    };

    w.writeBytes("RIFF", 4);
    u32le(36 + dataBytes);
    w.writeBytes("WAVE", 4);
    w.writeBytes("fmt ", 4);
    u32le(16);
    u16le(1); // PCM
    u16le(channels_);
    u32le(sampleRate);
    u32le(sampleRate * blockAlign);
    u16le(blockAlign);
    u16le(16);
    w.writeBytes("data", 4);
    u32le(dataBytes);
    for (int16_t s : pcm) {
        u16le(uint16_t(s));
    }
    return w.bytes();
}

} // namespace m2audio
