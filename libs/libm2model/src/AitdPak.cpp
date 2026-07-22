#include "m2model/AitdPak.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

#include "../third_party/pak_explode.h"

namespace m2model {

namespace {
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}
uint16_t le16(const uint8_t* p) {
    return uint16_t(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
}

// One entry's on-disc header (Shared/PakArchive.cs GetEntry):
//   u32 skip                      (0, or 4 + extra bytes)
//   u8  extra[skip - 4]           (AITD2/3 mask metadata; absent when skip==0)
//   u32 compressedSize
//   u32 uncompressedSize
//   u8  compressionType
//   u8  compressionFlags
//   u16 padding                   ("Offset" field)
// Payload begins at entryOffset + 16 + extraLen + padding.
struct EntryHeader {
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint8_t compressionType = 0;
    uint8_t compressionFlags = 0;
    size_t payloadOffset = 0;
    bool valid = false;
};

EntryHeader parseHeader(const std::vector<uint8_t>& d, size_t o) {
    EntryHeader h;
    if (o + 4 > d.size()) {
        return h;
    }
    uint32_t skip = le32(d.data() + o);
    size_t extraLen = (skip != 0) ? size_t(skip - 4) : 0;
    size_t fields = o + 4 + extraLen; // start of compressedSize
    if (fields + 12 > d.size()) {
        return h;
    }
    h.compressedSize = le32(d.data() + fields);
    h.uncompressedSize = le32(d.data() + fields + 4);
    h.compressionType = d[fields + 8];
    h.compressionFlags = d[fields + 9];
    uint16_t padding = le16(d.data() + fields + 10);
    h.payloadOffset = o + 16 + extraLen + padding;
    h.valid = h.payloadOffset <= d.size();
    return h;
}
} // namespace

AitdPak AitdPak::open(std::vector<uint8_t> bytes) {
    AitdPak pak;
    pak.data_ = std::move(bytes);
    if (pak.data_.size() < 8) {
        throw std::runtime_error("AITD PAK: file too small");
    }
    // The offset table starts at byte 4; that first value is both the first
    // entry's offset and the table's size, so count = value/4 - 1.
    uint32_t first = le32(pak.data_.data() + 4);
    if (first < 8 || first > pak.data_.size()) {
        throw std::runtime_error("AITD PAK: bad offset table");
    }
    size_t count = first / 4 - 1;
    pak.offsets_.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        size_t tablePos = 4 + i * 4;
        if (tablePos + 4 > pak.data_.size()) {
            break;
        }
        uint32_t off = le32(pak.data_.data() + tablePos);
        if (off == 0) {
            break; // terminator (some later games)
        }
        pak.offsets_.push_back(off);
    }
    return pak;
}

AitdPak AitdPak::openFromFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("could not open PAK: " + path.string());
    }
    return open(std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                      std::istreambuf_iterator<char>()));
}

uint8_t AitdPak::compressionType(size_t index) const {
    if (index >= offsets_.size()) {
        return 0xFF;
    }
    EntryHeader h = parseHeader(data_, offsets_[index]);
    return h.valid ? h.compressionType : 0xFF;
}

std::vector<uint8_t> AitdPak::read(size_t index) const {
    if (index >= offsets_.size()) {
        return {};
    }
    EntryHeader h = parseHeader(data_, offsets_[index]);
    if (!h.valid) {
        return {};
    }
    size_t avail = data_.size() - h.payloadOffset;
    size_t compAvail = std::min<size_t>(h.compressedSize ? h.compressedSize : avail, avail);
    const uint8_t* src = data_.data() + h.payloadOffset;

    switch (h.compressionType) {
        case 0: { // stored
            size_t n = std::min<size_t>(h.uncompressedSize ? h.uncompressedSize : compAvail,
                                         avail);
            return std::vector<uint8_t>(src, src + n);
        }
        case 1: { // PKWARE explode
            if (h.uncompressedSize == 0) {
                return {};
            }
            std::vector<uint8_t> out(h.uncompressedSize);
            // A defensive copy: the decoder walks src up to compressedSize
            // and reads zero past the end via its NEXTBYTE guard.
            std::vector<uint8_t> in(src, src + compAvail);
            PAK_explode(in.data(), out.data(), uint32_t(in.size()),
                         h.uncompressedSize, h.compressionFlags);
            return out;
        }
        default:
            return {}; // type 4 (deflate) not wired up yet
    }
}

} // namespace m2model
