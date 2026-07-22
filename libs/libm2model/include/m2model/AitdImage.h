#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace m2model {

// Alone in the Dark pre-rendered background images.
//
// The 3DO builds store room backdrops in three interchangeable containers
// that share one payload layout:
//   .pics  — camera backdrops, one or more pages padded to 64 KiB each
//   .bob   — a single un-padded page
//   .pad   — a single page padded to 64 KiB
//
// Page layout (little-endian):
//   u16 width          (320 on every file seen)
//   u16 height         (200)
//   u8  palette[768]   256 RGB triples, 8 bits per channel
//   u8  pixels[w*h]    one palette index per pixel
//
// A single un-padded page is therefore exactly 4 + 768 + 64000 = 64772
// bytes, which is the exact size of CAM8003.BOB — the check that
// confirmed the layout.
struct AitdImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba; // width*height*4
};

// Decodes every page in the blob. Returns an empty vector if the data does
// not look like this format.
std::vector<AitdImage> parseAitdImages(const std::vector<uint8_t>& data);
std::vector<AitdImage> loadAitdImages(const std::filesystem::path& path);

// True if the leading page header, together with the file's total size, is
// consistent with the layout above. Cheap enough for file-type sniffing.
//
// `header`/`headerSize` need only cover the first four bytes — the width
// and height. `fileSize` is the size of the whole file, which is what
// distinguishes a real page from four coincidental bytes. Keeping the two
// separate matters: a sniffer reads a small header, not the file, and an
// earlier signature that took only (data, size) silently rejected every
// backdrop because the sniffer's 12-byte buffer could never satisfy it.
bool looksLikeAitdImage(const uint8_t* header, size_t headerSize, uint64_t fileSize);

} // namespace m2model
