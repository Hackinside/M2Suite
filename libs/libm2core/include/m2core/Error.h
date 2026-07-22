#pragma once

#include <stdexcept>
#include <string>

namespace m2core {

// Thrown when input bytes don't match the expected M2/IFF container shape
// (bad magic, truncated chunk, size mismatch, etc).
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

// Thrown by decode paths that are intentionally not implemented yet
// (e.g. RLE-compressed or non-8bpp-indexed texel formats) rather than
// silently producing wrong pixels for a guessed layout.
class NotImplementedError : public std::runtime_error {
public:
    explicit NotImplementedError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace m2core
