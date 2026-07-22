#include "m2model/AitdImage.h"

#include <fstream>

namespace m2model {

namespace {

constexpr size_t kHeader = 4;
constexpr size_t kPalette = 768;
constexpr size_t kPage = 65536; // .pics/.pad pad each page to 64 KiB

// Only 320x200 has ever been observed, but accept anything plausible so a
// higher-resolution variant would still decode.
bool plausibleSize(uint32_t w, uint32_t h) {
    return w >= 8 && h >= 8 && w <= 1024 && h <= 1024;
}

uint16_t rdU16(const uint8_t* p) {
    return uint16_t(p[0] | (uint16_t(p[1]) << 8));
}

} // namespace

bool looksLikeAitdImage(const uint8_t* data, size_t size) {
    if (size < kHeader + kPalette + 64) {
        return false;
    }
    uint32_t w = rdU16(data);
    uint32_t h = rdU16(data + 2);
    if (!plausibleSize(w, h)) {
        return false;
    }
    size_t need = kHeader + kPalette + size_t(w) * h;
    // Either the file is exactly one un-padded page, or it is a whole
    // number of 64 KiB pages that each have room for the payload.
    if (size == need) {
        return true;
    }
    return need <= kPage && size % kPage == 0;
}

std::vector<AitdImage> parseAitdImages(const std::vector<uint8_t>& data) {
    std::vector<AitdImage> out;
    if (!looksLikeAitdImage(data.data(), data.size())) {
        return out;
    }
    uint32_t w = rdU16(data.data());
    uint32_t h = rdU16(data.data() + 2);
    size_t need = kHeader + kPalette + size_t(w) * h;
    size_t stride = (data.size() == need) ? need : kPage;
    size_t pages = data.size() / stride;

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
