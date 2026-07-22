#include "m2audio/Wav.h"

#include <cstring>
#include <fstream>
#include <string>

#include "m2core/Error.h"

namespace m2audio {

using m2core::FormatError;
using m2core::NotImplementedError;

namespace {
// WAV is little-endian throughout (unlike everything else in this suite).
uint32_t u32le(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}
uint16_t u16le(const uint8_t* p) {
    return uint16_t(p[0] | (p[1] << 8));
}
} // namespace

Wav Wav::load(const uint8_t* data, size_t size) {
    if (size < 12 || std::memcmp(data, "RIFF", 4) != 0 ||
        std::memcmp(data + 8, "WAVE", 4) != 0) {
        throw FormatError("not a RIFF/WAVE file");
    }

    Wav wav;
    bool sawFmt = false;
    size_t pos = 12;
    while (pos + 8 <= size) {
        const uint8_t* hdr = data + pos;
        uint32_t chunkSize = u32le(hdr + 4);
        const uint8_t* body = hdr + 8;
        if (pos + 8 + chunkSize > size) {
            chunkSize = uint32_t(size - pos - 8); // tolerate truncated tail
        }

        if (std::memcmp(hdr, "fmt ", 4) == 0 && chunkSize >= 16) {
            uint16_t format = u16le(body);
            if (format != 1) { // PCM
                throw NotImplementedError("WAV format tag " + std::to_string(format) +
                                           " not supported (only PCM)");
            }
            wav.channels_ = u16le(body + 2);
            wav.sampleRate_ = u32le(body + 4);
            wav.bitsPerSample_ = u16le(body + 14);
            sawFmt = true;
        } else if (std::memcmp(hdr, "data", 4) == 0) {
            wav.data_.assign(body, body + chunkSize);
        }
        pos += 8 + chunkSize + (chunkSize & 1); // RIFF chunks pad to even
    }

    if (!sawFmt || wav.channels_ == 0) {
        throw FormatError("WAV file has no valid fmt chunk");
    }
    if (wav.data_.empty()) {
        throw FormatError("WAV file has no data chunk");
    }
    return wav;
}

Wav Wav::loadFromFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw FormatError("could not open file: " + path.string());
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
    return load(bytes.data(), bytes.size());
}

std::vector<int16_t> Wav::decodePcm16() const {
    std::vector<int16_t> out;
    if (bitsPerSample_ == 16) {
        out.reserve(data_.size() / 2);
        for (size_t i = 0; i + 1 < data_.size(); i += 2) {
            out.push_back(int16_t(u16le(data_.data() + i)));
        }
    } else if (bitsPerSample_ == 8) {
        // WAV 8-bit is unsigned (unlike AIFF's signed 8-bit).
        out.reserve(data_.size());
        for (uint8_t b : data_) {
            out.push_back(int16_t((int(b) - 128) << 8));
        }
    } else {
        throw NotImplementedError("WAV decode: " + std::to_string(bitsPerSample_) +
                                   "-bit samples not supported (only 8/16-bit PCM)");
    }
    return out;
}

} // namespace m2audio
