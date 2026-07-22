#include "m2model/AitdImage.h"

#include <fstream>

namespace m2model {

namespace {

constexpr size_t kHeader = 4;
constexpr size_t kPalette = 768;
// Pages are padded up to a 4 KiB boundary, not to a fixed page size. This
// was originally hard-coded to 64 KiB because every AITD2 backdrop sampled
// happened to be 320x200 (payload 64772, rounding to exactly 65536). It is
// wrong the moment the dimensions differ: AITD1's camera00.pics is 240x200
// (48772 -> 49152) and AITD2's camera01.pics is 320x250 (80772 -> 81920),
// and both were rejected outright.
constexpr uint64_t kPageAlign = 4096;

uint64_t pageStride(uint64_t payload) {
    return ((payload + kPageAlign - 1) / kPageAlign) * kPageAlign;
}

// Only 320x200 has ever been observed, but accept anything plausible so a
// higher-resolution variant would still decode.
bool plausibleSize(uint32_t w, uint32_t h) {
    return w >= 8 && h >= 8 && w <= 1024 && h <= 1024;
}

uint16_t rdU16(const uint8_t* p) {
    return uint16_t(p[0] | (uint16_t(p[1]) << 8));
}

} // namespace

bool looksLikeAitdImage(const uint8_t* header, size_t headerSize, uint64_t fileSize) {
    if (headerSize < kHeader || fileSize < kHeader + kPalette + 64) {
        return false;
    }
    uint32_t w = rdU16(header);
    uint32_t h = rdU16(header + 2);
    if (!plausibleSize(w, h)) {
        return false;
    }
    uint64_t need = kHeader + kPalette + uint64_t(w) * h;
    // Either the file is exactly one un-padded page (.bob), or it is a
    // whole number of pages padded to the 4 KiB boundary (.pics/.pad).
    if (fileSize == need) {
        return true;
    }
    return fileSize % pageStride(need) == 0;
}

std::vector<AitdImage> parseAitdImages(const std::vector<uint8_t>& data) {
    std::vector<AitdImage> out;
    if (!looksLikeAitdImage(data.data(), data.size(), data.size())) {
        return out;
    }
    uint32_t w = rdU16(data.data());
    uint32_t h = rdU16(data.data() + 2);
    size_t need = kHeader + kPalette + size_t(w) * h;
    // The stride comes from the FIRST page's dimensions; every page in a
    // file shares them, which is what makes a single stride correct.
    size_t stride = (data.size() == need) ? need : size_t(pageStride(need));
    size_t pages = stride ? data.size() / stride : 0;

    for (size_t p = 0; p < pages; ++p) {
        size_t base = p * stride;
        // Later pages carry their own header and palette; a page whose
        // header disagrees is padding, so stop rather than emit garbage.
        uint32_t pw = rdU16(data.data() + base);
        uint32_t ph = rdU16(data.data() + base + 2);
        if (!plausibleSize(pw, ph) || base + kHeader + kPalette + size_t(pw) * ph > data.size()) {
            break;
        }
        const uint8_t* pal = data.data() + base + kHeader;
        const uint8_t* px = pal + kPalette;

        AitdImage img;
        img.width = pw;
        img.height = ph;
        img.rgba.resize(size_t(pw) * ph * 4);
        for (size_t i = 0; i < size_t(pw) * ph; ++i) {
            uint8_t idx = px[i];
            img.rgba[i * 4 + 0] = pal[idx * 3 + 0];
            img.rgba[i * 4 + 1] = pal[idx * 3 + 1];
            img.rgba[i * 4 + 2] = pal[idx * 3 + 2];
            img.rgba[i * 4 + 3] = 0xFF;
        }
        out.push_back(std::move(img));
    }
    return out;
}

std::vector<AitdImage> loadAitdImages(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    return parseAitdImages(bytes);
}

} // namespace m2model
