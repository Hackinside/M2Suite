#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "m2cel/Cel.h"

namespace m2cel {

// 3DO ANIM cel animation (form3do.h AnimChunk): an 'ANIM' info chunk
// followed by frame chunks. animType 1 = one shared 'CCB ' followed by
// per-frame 'PDAT' (and optional 'PLUT'); animType 0 = each frame carries
// its own CCB. Frames decode via the same cel pipeline as single .cel
// files.
class Anim {
public:
    static Anim load(const uint8_t* data, size_t size);
    static Anim loadFromFile(const std::filesystem::path& path);

    uint32_t version = 0;
    uint32_t animType = 0; // 0 = multi-CCB, 1 = single-CCB
    uint32_t numFrames = 0;
    uint32_t frameRate = 0; // 1/60s per frame

    struct Frame {
        CcbHeader ccb;
        std::vector<uint8_t> pdat;
        std::vector<uint16_t> plut;
    };
    const std::vector<Frame>& frames() const { return frames_; }

    std::vector<m2texture::Rgba8> decodeFrame(size_t index) const;

private:
    std::vector<Frame> frames_;
};

} // namespace m2cel
