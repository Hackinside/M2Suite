#include "m2core/ByteStream.h"

#include "m2core/Error.h"

namespace m2core {

void ByteReader::require(size_t n) const {
    if (n > remaining()) {
        throw FormatError("ByteReader: read past end of buffer (" +
                           std::to_string(n) + " bytes requested, " +
                           std::to_string(remaining()) + " remaining)");
    }
}

void ByteReader::seek(size_t pos) {
    if (pos > size_) {
        throw FormatError("ByteReader: seek past end of buffer");
    }
    pos_ = pos;
}

void ByteReader::skip(size_t n) {
    require(n);
    pos_ += n;
}

uint8_t ByteReader::readU8() {
    require(1);
    return data_[pos_++];
}

uint16_t ByteReader::readU16BE() {
    require(2);
    uint16_t v = (uint16_t(data_[pos_]) << 8) | uint16_t(data_[pos_ + 1]);
    pos_ += 2;
    return v;
}

uint32_t ByteReader::readU32BE() {
    require(4);
    uint32_t v = (uint32_t(data_[pos_]) << 24) | (uint32_t(data_[pos_ + 1]) << 16) |
                 (uint32_t(data_[pos_ + 2]) << 8) | uint32_t(data_[pos_ + 3]);
    pos_ += 4;
    return v;
}

float ByteReader::readF32BE() {
    uint32_t bits = readU32BE();
    float v;
    static_assert(sizeof(v) == sizeof(bits), "float must be 32 bits");
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

void ByteReader::readBytes(void* dst, size_t n) {
    require(n);
    std::memcpy(dst, data_ + pos_, n);
    pos_ += n;
}

std::vector<uint8_t> ByteReader::readBytes(size_t n) {
    require(n);
    std::vector<uint8_t> out(data_ + pos_, data_ + pos_ + n);
    pos_ += n;
    return out;
}

const uint8_t* ByteReader::dataAt(size_t pos) const {
    if (pos > size_) {
        throw FormatError("ByteReader: dataAt past end of buffer");
    }
    return data_ + pos;
}

void ByteWriter::writeU8(uint8_t v) { buf_.push_back(v); }

void ByteWriter::writeU16BE(uint16_t v) {
    buf_.push_back(uint8_t(v >> 8));
    buf_.push_back(uint8_t(v & 0xFF));
}

void ByteWriter::writeU32BE(uint32_t v) {
    buf_.push_back(uint8_t((v >> 24) & 0xFF));
    buf_.push_back(uint8_t((v >> 16) & 0xFF));
    buf_.push_back(uint8_t((v >> 8) & 0xFF));
    buf_.push_back(uint8_t(v & 0xFF));
}

void ByteWriter::writeF32BE(float v) {
    uint32_t bits;
    static_assert(sizeof(v) == sizeof(bits), "float must be 32 bits");
    std::memcpy(&bits, &v, sizeof(bits));
    writeU32BE(bits);
}

void ByteWriter::writeBytes(const void* src, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(src);
    buf_.insert(buf_.end(), p, p + n);
}

void ByteWriter::patchU32BE(size_t pos, uint32_t v) {
    if (pos + 4 > buf_.size()) {
        throw FormatError("ByteWriter: patchU32BE out of range");
    }
    buf_[pos] = uint8_t((v >> 24) & 0xFF);
    buf_[pos + 1] = uint8_t((v >> 16) & 0xFF);
    buf_[pos + 2] = uint8_t((v >> 8) & 0xFF);
    buf_[pos + 3] = uint8_t(v & 0xFF);
}

} // namespace m2core
