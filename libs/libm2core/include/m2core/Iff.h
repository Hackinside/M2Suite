#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "m2core/ByteStream.h"

namespace m2core {

// Minimal EA-IFF-85-style "FORM" chunk container reader/writer, purpose-built
// for M2's usage pattern (UTF/M2TX textures, and later AIFF audio, both of
// which are single FORM containers holding a flat sequence of property
// chunks — see M2TXiff.c's ProcessTXTR()/M2TX_WriteChunkData()).
//
// This is intentionally NOT a port of the full Portfolio-OS "iff" folio
// (reference/sdk-mercury/ifflib/iff.c, ~1800 lines) — that API is a general
// multi-context parser stack coupled to Portfolio kernel Item/Node/List
// primitives (see ifflib/iff.h's IFFParser/ContextNode), needed for things
// like LIST/CAT/PROP nesting and streaming/paused parsing that M2TX never
// uses. Reimplementing that whole OS-level parser would pull in kernel
// primitives with no meaning outside the M2 OS. What's ported here is the
// on-disk chunk *shape* (verified against M2TXiff.c's PushChunk/WriteChunk/
// PopChunk/FindPropChunk call sites), not the OS API surface.
//
// On-disk shape (big-endian):
//   'FORM' <size:u32> <formType:4cc> { <chunkId:4cc> <size:u32> <data[size]> <pad> }*
// Each chunk is padded so the next chunk starts on an `alignment`-byte
// boundary. M2's IFF folio defaults to 4-byte alignment (see
// reference/sdk-mercury/ifflib/iff.h: IFF_DEFAULT_ALIGNMENT == 4) rather
// than classic Amiga IFF-85's 2-byte/even padding — this reader/writer use
// 4-byte alignment to match.

constexpr uint32_t makeId(char a, char b, char c, char d) {
    return (uint32_t(uint8_t(a)) << 24) | (uint32_t(uint8_t(b)) << 16) |
           (uint32_t(uint8_t(c)) << 8) | uint32_t(uint8_t(d));
}

std::string idToString(uint32_t id);

constexpr uint32_t kIffDefaultAlignment = 4;
constexpr uint32_t ID_FORM = makeId('F', 'O', 'R', 'M');
constexpr uint32_t ID_CAT = makeId('C', 'A', 'T', ' ');

struct IffChunk {
    uint32_t id = 0;
    std::vector<uint8_t> data;
};

// Parses one top-level FORM container. Does not support nested
// FORM/LIST/CAT (M2TX and AIFF files don't use them).
class IffForm {
public:
    // Parses a FORM starting at the reader's current position. Throws
    // FormatError if the data isn't a well-formed 'FORM' container.
    //
    // `alignment` is the chunk padding boundary: 4 (default) for M2's IFF
    // folio files (UTF textures — IFF_DEFAULT_ALIGNMENT in ifflib/iff.h),
    // 2 for classic EA-IFF-85 files (AIFF/AIFC audio).
    static IffForm parse(ByteReader& reader, uint32_t alignment = kIffDefaultAlignment);

    // Parses either a single FORM or a 'CAT ' concatenation container
    // holding multiple FORMs (e.g. multi-texture .utf files: 'CAT ' <size>
    // 'TXTR' followed by FORM TXTR entries). Returns every FORM found.
    static std::vector<IffForm> parseAll(ByteReader& reader,
                                          uint32_t alignment = kIffDefaultAlignment);

    uint32_t formType() const { return formType_; }
    const std::vector<IffChunk>& chunks() const { return chunks_; }

    bool has(uint32_t chunkId) const;
    // Returns nullptr if the chunk isn't present, matching FindPropChunk()'s
    // "absent means not-an-error, caller supplies a default" convention.
    const std::vector<uint8_t>* find(uint32_t chunkId) const;

private:
    uint32_t formType_ = 0;
    std::vector<IffChunk> chunks_;
};

// Builds one flat FORM container (mirrors PushChunk(FORM,type)/
// WriteChunk/PushChunk(id)/WriteChunk/PopChunk/.../PopChunk in
// M2TX_WriteChunkData, minus the nested-context stack since M2TX/AIFF only
// ever nest one level deep: FORM -> flat chunks).
class IffFormWriter {
public:
    explicit IffFormWriter(uint32_t formType, uint32_t alignment = kIffDefaultAlignment);

    void addChunk(uint32_t chunkId, const std::vector<uint8_t>& data);
    void addChunk(uint32_t chunkId, const void* data, size_t size);

    // Serializes the complete 'FORM' <size> <formType> {chunks...} buffer.
    std::vector<uint8_t> finish() const;

private:
    uint32_t formType_;
    uint32_t alignment_;
    std::vector<IffChunk> chunks_;
};

} // namespace m2core
