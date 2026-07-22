#include "m2core/Iff.h"

#include "m2core/Error.h"

namespace m2core {

std::string idToString(uint32_t id) {
    std::string s(4, ' ');
    s[0] = char((id >> 24) & 0xFF);
    s[1] = char((id >> 16) & 0xFF);
    s[2] = char((id >> 8) & 0xFF);
    s[3] = char(id & 0xFF);
    return s;
}

namespace {
constexpr uint32_t kSizeReservedThreshold = 0xfffffff0u;

size_t padded(size_t n, uint32_t alignment) {
    size_t rem = n % alignment;
    return rem == 0 ? n : n + (alignment - rem);
}
} // namespace

IffForm IffForm::parse(ByteReader& reader, uint32_t alignment) {
    uint32_t formId = reader.readU32BE();
    if (formId != ID_FORM) {
        throw FormatError("IffForm::parse: expected 'FORM', got '" + idToString(formId) + "'");
    }

    uint32_t formSize = reader.readU32BE();
    if (formSize >= kSizeReservedThreshold) {
        // IFF_SIZE_UNKNOWN_32/64 and IFF_SIZE_64BIT sentinels (see
        // ifflib/iff.h) only ever appear mid-write, backpatched before the
        // file is closed. A concrete file with a reserved size value here
        // is either truncated or not produced by M2TX_WriteToIFF.
        throw FormatError("IffForm::parse: FORM has an unresolved/reserved size sentinel");
    }

    size_t formEnd = reader.position() + formSize;
    if (formEnd > reader.size()) {
        throw FormatError("IffForm::parse: FORM size extends past end of file");
    }

    IffForm form;
    form.formType_ = reader.readU32BE();

    while (reader.position() < formEnd) {
        if (formEnd - reader.position() < 8) {
            throw FormatError("IffForm::parse: truncated chunk header");
        }
        IffChunk chunk;
        chunk.id = reader.readU32BE();
        uint32_t chunkSize = reader.readU32BE();
        if (chunkSize >= kSizeReservedThreshold) {
            throw FormatError("IffForm::parse: chunk '" + idToString(chunk.id) +
                               "' has an unresolved/reserved size sentinel");
        }
        if (reader.position() + chunkSize > formEnd) {
            // Real-world tolerance: several disc files (e.g. imsaM2's
            // voiceover AIFFs) end with a chunk — typically 'APPL' — whose
            // stored size overruns the FORM. Hardware-era parsers read
            // what's there and moved on; clamp instead of rejecting the
            // whole file.
            chunkSize = uint32_t(formEnd - reader.position());
        }
        chunk.data = reader.readBytes(chunkSize);
        form.chunks_.push_back(std::move(chunk));

        size_t alignedPos = padded(reader.position(), alignment);
        if (alignedPos > formEnd) {
            // Trailing pad bytes belong to the container beyond this FORM;
            // clamp so we don't walk past a FORM that ends exactly at the
            // file boundary with no room for alignment padding.
            alignedPos = formEnd;
        }
        reader.seek(alignedPos);
    }

    return form;
}

std::vector<IffForm> IffForm::parseAll(ByteReader& reader, uint32_t alignment) {
    std::vector<IffForm> forms;

    // Peek the container id without consuming it.
    size_t start = reader.position();
    uint32_t id = reader.readU32BE();

    if (id == ID_FORM) {
        reader.seek(start);
        forms.push_back(parse(reader, alignment));
        return forms;
    }

    if (id != ID_CAT) {
        throw FormatError("IffForm::parseAll: expected 'FORM' or 'CAT ', got '" +
                           idToString(id) + "'");
    }

    uint32_t catSize = reader.readU32BE();
    size_t catEnd;
    if (catSize >= kSizeReservedThreshold) {
        // Real-world quirk (e.g. multimega's road01e.utf): the writer left
        // IFF_SIZE_UNKNOWN_32 (0xffffffff) unbackpatched in the CAT size
        // field. Hardware-era readers tolerated it; treat the CAT as
        // extending to end of file.
        catEnd = reader.size();
    } else {
        catEnd = reader.position() + catSize;
        if (catEnd > reader.size()) {
            throw FormatError("IffForm::parseAll: CAT size extends past end of file");
        }
    }
    reader.skip(4); // contents type hint (e.g. 'TXTR') — informational

    while (reader.position() + 8 <= catEnd) {
        forms.push_back(parse(reader, alignment));
        size_t alignedPos = padded(reader.position(), alignment);
        reader.seek(alignedPos > catEnd ? catEnd : alignedPos);
    }
    if (forms.empty()) {
        throw FormatError("IffForm::parseAll: CAT container holds no FORMs");
    }
    return forms;
}

bool IffForm::has(uint32_t chunkId) const {
    return find(chunkId) != nullptr;
}

const std::vector<uint8_t>* IffForm::find(uint32_t chunkId) const {
    for (const auto& c : chunks_) {
        if (c.id == chunkId) {
            return &c.data;
        }
    }
    return nullptr;
}

IffFormWriter::IffFormWriter(uint32_t formType, uint32_t alignment)
    : formType_(formType), alignment_(alignment) {}

void IffFormWriter::addChunk(uint32_t chunkId, const std::vector<uint8_t>& data) {
    chunks_.push_back(IffChunk{chunkId, data});
}

void IffFormWriter::addChunk(uint32_t chunkId, const void* data, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    chunks_.push_back(IffChunk{chunkId, std::vector<uint8_t>(p, p + size)});
}

std::vector<uint8_t> IffFormWriter::finish() const {
    ByteWriter w;
    w.writeU32BE(ID_FORM);
    size_t sizePatchPos = w.size();
    w.writeU32BE(0); // patched below once the body length is known
    w.writeU32BE(formType_);

    for (const auto& chunk : chunks_) {
        w.writeU32BE(chunk.id);
        w.writeU32BE(uint32_t(chunk.data.size()));
        w.writeBytes(chunk.data.data(), chunk.data.size());
        size_t alignedSize = padded(w.size(), alignment_);
        while (w.size() < alignedSize) {
            w.writeU8(0);
        }
    }

    uint32_t bodySize = uint32_t(w.size() - sizePatchPos - 4);
    w.patchU32BE(sizePatchPos, bodySize);
    return w.bytes();
}

} // namespace m2core
