#include "m2disc/OperaDisc.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace m2disc {

namespace {

[[noreturn]] void fail(const std::string& msg) {
    throw std::runtime_error(msg);
}

uint32_t u32be(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// 3DO filesystem constants (SDK filesystem.h / discdata.h).
constexpr uint8_t kRecordTypeLabel = 0x01;
constexpr uint8_t kSyncByte = 0x5A;
constexpr uint32_t kDefaultBlockSize = 2048;
constexpr uint32_t kDirLastInDir = 0x80000000u;
constexpr uint32_t kDirLastInBlock = 0x40000000u;
constexpr uint32_t kDirFlagDirectory = 0x00000001u;

std::vector<uint8_t> readWholeFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        fail("could not open image file: " + path.string());
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
}

// Replaces characters illegal on Windows / not representable, so Japanese
// (Shift-JIS) disc names — which broke the 3dt tool with "invalid utf8" —
// still extract to a usable path. Keeps ASCII letters/digits and a small
// safe punctuation set; everything else becomes '_'. Empty names get a
// placeholder.
std::string sanitizeName(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (unsigned char c : raw) {
        if (c == 0) {
            break;
        }
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            c == '.' || c == '_' || c == '-' || c == ' ' || c == '(' || c == ')' ||
            c == '+' || c == '&' || c == '!' || c == '#') {
            out.push_back(char(c));
        } else {
            out.push_back('_');
        }
    }
    // Trim trailing spaces/dots (illegal at end of a Windows path component).
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
        out.pop_back();
    }
    if (out.empty()) {
        out = "_unnamed_";
    }
    return out;
}

} // namespace

struct OperaDisc::DirRecord {
    uint32_t flags = 0;
    uint32_t type = 0;
    uint32_t blockSize = 0;
    uint32_t byteCount = 0;
    uint32_t blockCount = 0;
    uint32_t block = 0; // avatar_list[0]
    std::string name;
    bool isDirectory() const { return (flags & kDirFlagDirectory) != 0; }
    bool lastInDir() const { return (flags & kDirLastInDir) != 0; }
    bool lastInBlock() const { return (flags & kDirLastInBlock) != 0; }
};

OperaDisc OperaDisc::open(const std::filesystem::path& imagePath) {
    OperaDisc disc;

    // CHD detection: "MComprHD" magic. Full CHD support (zlib/lzma/flac/cdlz
    // hunk codecs) is a separate undertaking; surface a clear message rather
    // than misparsing the compressed bytes.
    {
        std::ifstream probe(imagePath, std::ios::binary);
        char magic[8] = {};
        if (probe) {
            probe.read(magic, 8);
        }
        if (std::memcmp(magic, "MComprHD", 8) == 0) {
            fail("CHD images aren't supported yet — please convert to .bin/.cue or "
                 ".iso first (e.g. `chdman extractcd`).");
        }
    }

    disc.image_ = readWholeFile(imagePath);
    if (disc.image_.size() < kDefaultBlockSize) {
        fail("image too small to be a 3DO disc");
    }

    // Locate the Opera disc label and infer the raw/cooked sector layout by
    // scanning the first ~32 sectors for the signature:
    //   dl_RecordType == 1, then 5 x 0x5A sync bytes, then version 1.
    // For cooked 2048 images this sits at byte 0; for 2352 raw Mode 1 at
    // byte 16; Mode 2 at byte 24. The distance from a 2352-aligned boundary
    // yields the per-sector header size.
    bool found = false;
    const size_t scanLimit = std::min<size_t>(disc.image_.size() - 8, 2352 * 32);
    for (size_t off = 0; off <= scanLimit; ++off) {
        const uint8_t* p = disc.image_.data() + off;
        if (p[0] == kRecordTypeLabel && p[1] == kSyncByte && p[2] == kSyncByte &&
            p[3] == kSyncByte && p[4] == kSyncByte && p[5] == kSyncByte && p[6] == 1) {
            // Determine geometry from where the label landed.
            if (off == 0) {
                disc.sectorSize_ = 2048;
                disc.sectorHeader_ = 0;
            } else {
                // Raw image: the label starts `sectorHeader` bytes into
                // sector 0 of a 2352-byte layout.
                disc.sectorSize_ = 2352;
                disc.sectorHeader_ = uint32_t(off);
            }
            found = true;
            break;
        }
    }
    if (!found) {
        fail("no 3DO Opera disc label found — not a 3DO disc image, or an "
             "unsupported/encrypted layout");
    }

    // Parse the DiscLabel (discdata.h layout) from user-data bytes.
    uint8_t label[128];
    disc.readUserBytes(0, label, sizeof(label));
    disc.blockSize_ = u32be(label + 76);
    if (disc.blockSize_ == 0 || disc.blockSize_ > 4096) {
        disc.blockSize_ = kDefaultBlockSize;
    }
    // dl_VolumeIdentifier at offset 40, 32 bytes.
    disc.volumeName_ = sanitizeName(
        std::string(reinterpret_cast<const char*>(label + 40),
                     strnlen(reinterpret_cast<const char*>(label + 40), 32)));
    disc.rootBlockCount_ = u32be(label + 88);   // dl_RootDirectoryBlockCount
    disc.rootBlockSize_ = u32be(label + 92);    // dl_RootDirectoryBlockSize
    if (disc.rootBlockSize_ == 0 || disc.rootBlockSize_ > 65536) {
        disc.rootBlockSize_ = disc.blockSize_;
    }
    // dl_RootDirectoryAvatarList[0] at offset 100.
    disc.rootBlock_ = u32be(label + 100);
    return disc;
}

size_t OperaDisc::readUserBytes(uint64_t userOffset, uint8_t* dst, size_t len) const {
    // Map logical 2048-byte user blocks onto the raw/cooked sector layout.
    // Returns the number of bytes actually read; anything past the end of a
    // (commonly truncated) dump is zero-filled so extraction still recovers
    // every complete file rather than aborting on the first bad tail block.
    size_t got = 0;
    while (len > 0) {
        uint64_t block = userOffset / kDefaultBlockSize;
        uint64_t within = userOffset % kDefaultBlockSize;
        uint64_t imgOff = block * sectorSize_ + sectorHeader_ + within;
        size_t chunk = size_t(std::min<uint64_t>(len, kDefaultBlockSize - within));
        if (imgOff >= image_.size()) {
            std::memset(dst, 0, len); // fully past end
            break;
        }
        size_t avail = std::min<size_t>(chunk, image_.size() - size_t(imgOff));
        std::memcpy(dst, image_.data() + imgOff, avail);
        if (avail < chunk) {
            std::memset(dst + avail, 0, chunk - avail);
        }
        got += avail;
        dst += chunk;
        userOffset += chunk;
        len -= chunk;
    }
    return got;
}

bool OperaDisc::blockAvailable(uint32_t block) const {
    uint64_t imgOff = uint64_t(block) * sectorSize_ + sectorHeader_;
    return imgOff + blockSize_ <= image_.size();
}

std::vector<uint8_t> OperaDisc::readBlocks(uint32_t block, uint32_t count) const {
    std::vector<uint8_t> out(size_t(count) * blockSize_);
    readUserBytes(uint64_t(block) * blockSize_, out.data(), out.size());
    return out;
}

void OperaDisc::readDirectory(
    uint32_t avatarBlock, uint32_t dirBlockSize, uint32_t dirBlockCount,
    const std::string& parentPath, const std::function<void(const Entry&)>& cb) const {
    if (dirBlockSize == 0) {
        dirBlockSize = blockSize_;
    }
    if (dirBlockCount == 0) {
        dirBlockCount = 1;
    }
    // Directory data lives at avatarBlock * discBlockSize, spanning
    // dirBlockCount blocks of dirBlockSize; dh_NextBlock chains by index
    // within that span (3dt FSWalker: base + dirBlockSize * next_block).
    const uint64_t dirBase = uint64_t(avatarBlock) * blockSize_;
    int32_t blockIndex = 0;
    int guard = 0;
    bool lastInDir = false;

    while (blockIndex >= 0 && uint32_t(blockIndex) < dirBlockCount && !lastInDir) {
        if (++guard > int(dirBlockCount) + 8) {
            break; // chain loop guard
        }
        std::vector<uint8_t> buf(dirBlockSize);
        if (readUserBytes(dirBase + uint64_t(blockIndex) * dirBlockSize, buf.data(),
                           buf.size()) < 20) {
            break;
        }
        int32_t nextBlock = int32_t(u32be(buf.data() + 0));
        uint32_t firstFree = u32be(buf.data() + 12);
        uint32_t firstEntry = u32be(buf.data() + 16);
        if (firstEntry < 20 || firstEntry > buf.size()) {
            break;
        }
        if (firstFree > buf.size()) {
            firstFree = uint32_t(buf.size());
        }

        // Records run from dh_FirstEntryOffset up to dh_FirstFreeByte
        // (authoritative end — the LAST_IN_BLOCK/DIR flags are set on the
        // final record too, but first_free_byte is what 3dt trusts).
        size_t pos = firstEntry;
        while (pos + 68 <= firstFree) {
            DirRecord rec;
            rec.flags = u32be(buf.data() + pos + 0);
            rec.type = u32be(buf.data() + pos + 8);
            rec.blockSize = u32be(buf.data() + pos + 12);
            rec.byteCount = u32be(buf.data() + pos + 16);
            rec.blockCount = u32be(buf.data() + pos + 20);
            uint32_t lastAvatar = u32be(buf.data() + pos + 64);
            rec.name.assign(reinterpret_cast<const char*>(buf.data() + pos + 32),
                             strnlen(reinterpret_cast<const char*>(buf.data() + pos + 32), 32));
            // Record = 68 fixed bytes + (lastAvatar+1) avatar words.
            size_t recSize = 68 + (size_t(lastAvatar) + 1) * 4;
            if (pos + recSize > firstFree) {
                break; // malformed tail
            }
            rec.block = u32be(buf.data() + pos + 68); // avatar_list[0]

            if (!rec.name.empty()) {
                Entry e;
                e.name = sanitizeName(rec.name);
                e.path = parentPath.empty() ? e.name : parentPath + "/" + e.name;
                e.isDirectory = rec.isDirectory();
                e.byteCount = rec.byteCount;
                e.block = rec.block;
                e.type = rec.type;
                e.entryBlockSize = rec.blockSize;
                e.blockCount = rec.blockCount;
                cb(e);
            }
            pos += recSize;

            if (rec.lastInDir()) {
                lastInDir = true;
                break;
            }
            if (rec.lastInBlock()) {
                break;
            }
        }

        blockIndex = nextBlock; // -1 terminates
    }
}

void OperaDisc::list(const std::function<void(const Entry&)>& cb) const {
    std::function<void(uint32_t, uint32_t, uint32_t, const std::string&)> recurse =
        [&](uint32_t block, uint32_t bs, uint32_t bc, const std::string& path) {
            readDirectory(block, bs, bc, path, [&](const Entry& e) {
                cb(e);
                if (e.isDirectory && e.block != 0 && e.block != block) {
                    recurse(e.block, e.entryBlockSize, e.blockCount, e.path);
                }
            });
        };
    recurse(rootBlock_, rootBlockSize_, rootBlockCount_, "");
}

std::vector<uint8_t> OperaDisc::readFile(const std::string& path) const {
    std::vector<uint8_t> result;
    bool found = false;
    std::function<void(uint32_t, uint32_t, uint32_t, const std::string&)> recurse =
        [&](uint32_t block, uint32_t bs, uint32_t bc, const std::string& parent) {
            if (found) {
                return;
            }
            readDirectory(block, bs, bc, parent, [&](const Entry& e) {
                if (found) {
                    return;
                }
                if (e.path == path && !e.isDirectory) {
                    uint32_t blocks = (e.byteCount + blockSize_ - 1) / blockSize_;
                    result = readBlocks(e.block, blocks);
                    result.resize(e.byteCount);
                    found = true;
                } else if (e.isDirectory && e.block != 0 && e.block != block) {
                    recurse(e.block, e.entryBlockSize, e.blockCount, e.path);
                }
            });
        };
    recurse(rootBlock_, rootBlockSize_, rootBlockCount_, "");
    if (!found) {
        fail("file not found in disc image: " + path);
    }
    return result;
}

int OperaDisc::extractAll(
    const std::filesystem::path& outDir,
    const std::function<void(int, const std::string&)>& progress) const {
    std::filesystem::create_directories(outDir);
    int written = 0;
    std::function<void(uint32_t, uint32_t, uint32_t, const std::filesystem::path&,
                        const std::string&)>
        recurse = [&](uint32_t block, uint32_t bs, uint32_t bc,
                       const std::filesystem::path& dir, const std::string& parentPath) {
            readDirectory(block, bs, bc, parentPath, [&](const Entry& e) {
                std::filesystem::path target = dir / e.name;
                if (e.isDirectory) {
                    if (e.block != 0 && e.block != block) {
                        std::error_code ec;
                        std::filesystem::create_directories(target, ec);
                        recurse(e.block, e.entryBlockSize, e.blockCount, target, e.path);
                    }
                } else {
                    uint32_t blocks = (e.byteCount + blockSize_ - 1) / blockSize_;
                    std::vector<uint8_t> data;
                    try {
                        data = readBlocks(e.block, blocks);
                        data.resize(e.byteCount);
                    } catch (...) {
                        return; // skip an unreadable record, keep extracting
                    }
                    std::ofstream out(target, std::ios::binary);
                    if (out) {
                        out.write(reinterpret_cast<const char*>(data.data()),
                                   std::streamsize(data.size()));
                        ++written;
                        if (progress) {
                            progress(written, e.path);
                        }
                    }
                }
            });
        };
    recurse(rootBlock_, rootBlockSize_, rootBlockCount_, outDir, "");
    return written;
}

// --------------------------------------------------------------------------
// Crystal Dynamics "bigfile"

BigFile BigFile::openFromFile(const std::filesystem::path& path) {
    BigFile bf;
    bf.path_ = path;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        fail("could not open bigfile: " + path.string());
    }
    uint8_t head[16];
    f.read(reinterpret_cast<char*>(head), sizeof(head));
    if (f.gcount() != std::streamsize(sizeof(head))) {
        fail("bigfile too small");
    }
    uint32_t count = u32be(head);
    if (count == 0 || count > 200000) {
        fail("bigfile: implausible entry count");
    }
    std::vector<uint8_t> table(size_t(count) * 12);
    f.read(reinterpret_cast<char*>(table.data()), std::streamsize(table.size()));
    if (f.gcount() != std::streamsize(table.size())) {
        fail("bigfile: truncated entry table");
    }
    bf.entries_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = table.data() + size_t(i) * 12;
        Entry en;
        en.id = u32be(e);
        en.size = u32be(e + 4);
        en.offset = u32be(e + 8);
        if (en.size == 0) {
            continue;
        }
        bf.entries_.push_back(en);
    }
    return bf;
}

std::vector<uint8_t> BigFile::read(size_t index) const {
    if (index >= entries_.size()) {
        return {};
    }
    const Entry& e = entries_[index];
    std::ifstream f(path_, std::ios::binary);
    if (!f) {
        return {};
    }
    f.seekg(std::streamoff(e.offset));
    std::vector<uint8_t> out(e.size);
    f.read(reinterpret_cast<char*>(out.data()), std::streamsize(out.size()));
    out.resize(size_t(std::max<std::streamsize>(f.gcount(), 0)));
    return out;
}

int BigFile::extractAll(const std::filesystem::path& outDir,
                         const std::function<void(int, const std::string&)>& progress) const {
    std::filesystem::create_directories(outDir);
    int written = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        std::vector<uint8_t> data = read(i);
        if (data.empty()) {
            continue;
        }
        char name[64];
        std::snprintf(name, sizeof(name), "entry_%04zu_%08x.bin", i, entries_[i].id);
        std::ofstream out(outDir / name, std::ios::binary);
        if (out) {
            out.write(reinterpret_cast<const char*>(data.data()),
                       std::streamsize(data.size()));
            ++written;
            if (progress) {
                progress(written, name);
            }
        }
    }
    return written;
}

} // namespace m2disc
