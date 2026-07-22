#include "m2cel/Anim.h"

#include <fstream>
#include <string>

#include "m2core/ByteStream.h"
#include "m2core/Error.h"
#include "m2core/Iff.h"

namespace m2cel {

using m2core::ByteReader;
using m2core::FormatError;

namespace {
constexpr uint32_t ID_ANIM = m2core::makeId('A', 'N', 'I', 'M');
constexpr uint32_t ID_CCB = m2core::makeId('C', 'C', 'B', ' ');
constexpr uint32_t ID_PDAT = m2core::makeId('P', 'D', 'A', 'T');
constexpr uint32_t ID_PLUT = m2core::makeId('P', 'L', 'U', 'T');

CcbHeader readCcbBody(ByteReader& r) {
    CcbHeader ccb;
    ccb.version = r.readU32BE();
    ccb.flags = r.readU32BE();
    ccb.nextPtr = r.readU32BE();
    ccb.sourcePtr = r.readU32BE();
    ccb.plutPtr = r.readU32BE();
    ccb.xPos = int32_t(r.readU32BE());
    ccb.yPos = int32_t(r.readU32BE());
    ccb.hdx = int32_t(r.readU32BE());
    ccb.hdy = int32_t(r.readU32BE());
    ccb.vdx = int32_t(r.readU32BE());
    ccb.vdy = int32_t(r.readU32BE());
    ccb.hddx = int32_t(r.readU32BE());
    ccb.hddy = int32_t(r.readU32BE());
    ccb.pixc = r.readU32BE();
    ccb.pre0 = r.readU32BE();
    ccb.pre1 = r.readU32BE();
    ccb.width = r.readU32BE();
    ccb.height = r.readU32BE();
    return ccb;
}

std::vector<uint16_t> readPlutBody(ByteReader& r, uint32_t bodySize) {
    uint32_t numEntries = r.readU32BE();
    if (numEntries > (bodySize - 4) / 2) {
        throw FormatError("ANIM PLUT entry count exceeds chunk size");
    }
    std::vector<uint16_t> plut(numEntries);
    for (auto& e : plut) e = r.readU16BE();
    return plut;
}
} // namespace

Anim Anim::load(const uint8_t* data, size_t size) {
    ByteReader r(data, size);
    Anim anim;
    bool sawAnim = false;

    // Shared state carried across chunks: the active CCB (single-CCB anims
    // define it once) and the most recent PLUT. Each PDAT closes a frame.
    CcbHeader currentCcb;
    bool haveCcb = false;
    std::vector<uint16_t> currentPlut;

    while (r.remaining() >= 8) {
        uint32_t id = r.readU32BE();
        uint32_t chunkSize = r.readU32BE();
        if (chunkSize < 8 || chunkSize - 8 > r.remaining()) {
            // Trailing data after the last frame is common (Street Fighter's
            // *.DAT cel chains append a non-chunk block after 26 frames). If
            // we already have frames, stop cleanly and keep them rather than
            // failing the whole animation.
            if (!anim.frames_.empty()) {
                break;
            }
            throw FormatError("anim chunk '" + m2core::idToString(id) +
                               "' has bad size " + std::to_string(chunkSize));
        }
        uint32_t bodySize = chunkSize - 8;
        size_t bodyStart = r.position();

        if (id == ID_ANIM) {
            if (bodySize < 16) {
                throw FormatError("ANIM chunk body smaller than 16 bytes");
            }
            anim.version = r.readU32BE();
            anim.animType = r.readU32BE();
            anim.numFrames = r.readU32BE();
            anim.frameRate = r.readU32BE();
            // startFrame, numLoops, LoopRec[] — not needed for still decode
            sawAnim = true;
        } else if (id == ID_CCB) {
            if (bodySize < 72) {
                throw FormatError("ANIM CCB chunk body smaller than 72 bytes");
            }
            currentCcb = readCcbBody(r);
            haveCcb = true;
        } else if (id == ID_PLUT) {
            if (bodySize < 4) {
                throw FormatError("ANIM PLUT chunk body smaller than 4 bytes");
            }
            currentPlut = readPlutBody(r, bodySize);
            // Two chunk orderings occur in the wild: PLUT-before-PDAT (the
            // palette applies to following frames, carried by currentPlut)
            // and PDAT-before-PLUT (StarBlade's boom.Anim: each frame's
            // palette trails its pixel data). For the latter, back-fill the
            // most recent frame that hasn't got a palette yet.
            if (!anim.frames_.empty() && anim.frames_.back().plut.empty()) {
                anim.frames_.back().plut = currentPlut;
            }
        } else if (id == ID_PDAT) {
            if (!haveCcb) {
                throw FormatError("ANIM PDAT appeared before any CCB chunk");
            }
            Frame frame;
            frame.ccb = currentCcb;
            frame.pdat = r.readBytes(bodySize);
            frame.plut = currentPlut;
            anim.frames_.push_back(std::move(frame));
        }
        // Chunks are quad-aligned (form3do.h: "Chunks must be Quad byte
        // alligned"); chunk sizes in practice already include padding.
        size_t next = bodyStart + bodySize;
        next = (next + 3) & ~size_t(3);
        if (next > r.size()) {
            next = r.size();
        }
        r.seek(next);
    }

    // A file may have no 'ANIM' chunk yet still be an animation: several
    // games (Yu Yu Hakusho's GRAPH/*.anim) just concatenate complete cels —
    // repeated CCB [+XTRA] + PDAT [+PLUT] groups. The loop above already
    // collects one frame per PDAT with the CCB that preceded it, so such a
    // file arrives here with >1 frame and simply needs the header
    // synthesised rather than being rejected as "not an ANIM".
    if (!sawAnim) {
        if (anim.frames_.size() < 2) {
            throw FormatError("not a 3DO ANIM file: no 'ANIM' chunk and only "
                               "one cel frame present");
        }
        anim.version = 0;
        anim.animType = 0;
        anim.numFrames = uint32_t(anim.frames_.size());
        anim.frameRate = 0; // unknown; the viewer applies its default
    }
    if (anim.frames_.empty()) {
        throw FormatError("ANIM file has no PDAT frame chunks");
    }
    return anim;
}

Anim Anim::loadFromFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw FormatError("could not open file: " + path.string());
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
    return load(bytes.data(), bytes.size());
}

std::vector<m2texture::Rgba8> Anim::decodeFrame(size_t index) const {
    if (index >= frames_.size()) {
        throw FormatError("ANIM frame index out of range");
    }
    const Frame& f = frames_[index];
    return decodeCelFrame(f.ccb, f.pdat, f.plut);
}

} // namespace m2cel
