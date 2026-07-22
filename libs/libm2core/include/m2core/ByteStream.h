#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace m2core {

// Bounds-checked, big-endian ("Motorola") byte-stream reader.
//
// The M2 texture (UTF) format and its underlying IFF container are
// explicitly big-endian on disk regardless of host byte order — see
// M2TXiff.c's putMemShort()/putMemLong() comment "UTF specifies Motorala
// or MSB 1st". This class only ever reads big-endian, independent of the
// host CPU, so callers never need `#ifdef INTEL` byte-swap branches like
// the original SDK source does.
class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    explicit ByteReader(const std::vector<uint8_t>& bytes)
        : ByteReader(bytes.data(), bytes.size()) {}

    size_t size() const { return size_; }
    size_t position() const { return pos_; }
    size_t remaining() const { return size_ - pos_; }
    bool atEnd() const { return pos_ >= size_; }

    void seek(size_t pos);
    void skip(size_t n);

    uint8_t readU8();
    uint16_t readU16BE();
    uint32_t readU32BE();
    float readF32BE();

    // Copies n bytes into dst. Throws FormatError if fewer than n bytes remain.
    void readBytes(void* dst, size_t n);
    std::vector<uint8_t> readBytes(size_t n);

    const uint8_t* dataAt(size_t pos) const;

private:
    void require(size_t n) const;

    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
};

// Growable big-endian byte-stream writer.
class ByteWriter {
public:
    void writeU8(uint8_t v);
    void writeU16BE(uint16_t v);
    void writeU32BE(uint32_t v);
    void writeF32BE(float v);
    void writeBytes(const void* src, size_t n);

    size_t size() const { return buf_.size(); }
    const std::vector<uint8_t>& bytes() const { return buf_; }

    // Overwrites 4 bytes at `pos` with a big-endian uint32 — used to
    // backpatch IFF chunk/FORM size fields once the chunk body is known.
    void patchU32BE(size_t pos, uint32_t v);

private:
    std::vector<uint8_t> buf_;
};

} // namespace m2core
