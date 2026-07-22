#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace m2disc {

// Reads the 3DO "Opera" read-only filesystem straight out of a disc image
// and extracts its files — no external tool required. Handles the common
// image containers seen in 3DO/M2 dumps:
//   * .iso / .img       — 2048-byte user-data sectors (already "cooked")
//   * .bin (+ optional .cue) — 2352-byte raw Mode 1/2 sectors
//   * CHD                — MAME compressed hunks (hunks of raw sectors)
// The label is located by scanning the first sectors for the record-type +
// 0x5A sync-byte signature, which also auto-detects the sector layout.
//
// Filesystem structures follow the 3DO SDK (filesystem.h / discdata.h):
// a DiscLabel names the root directory's block; each directory block is a
// DirectoryHeader followed by DirectoryRecords; a record's avatar list
// gives the block number(s) of the file/subdirectory contents.
class OperaDisc {
public:
    struct Entry {
        std::string name;
        std::string path;  // POSIX-style path from the disc root
        bool isDirectory = false;
        uint32_t byteCount = 0;
        uint32_t block = 0;          // first avatar block (disc block units)
        uint32_t type = 0;           // dir_Type fourcc
        uint32_t entryBlockSize = 0; // dir_BlockSize (directories: their block size)
        uint32_t blockCount = 0;     // dir_BlockCount (directories: chain bound)
    };

    // Opens an image file and locates the Opera volume. Throws on failure
    // (not an image we understand, or no Opera label found).
    static OperaDisc open(const std::filesystem::path& imagePath);

    const std::string& volumeName() const { return volumeName_; }
    uint32_t blockSize() const { return blockSize_; }

    // Walks the whole directory tree, invoking the callback for each entry.
    void list(const std::function<void(const Entry&)>& cb) const;

    // Reads one file's bytes by its root-relative path (as reported by
    // list()). Throws if the path isn't a file.
    std::vector<uint8_t> readFile(const std::string& path) const;

    // Extracts every file to outDir, recreating the directory tree.
    // Returns the number of files written. Non-UTF-8 / illegal filename
    // characters (common on Japanese discs) are sanitized. `progress` is
    // called with (filesWritten, currentPath) if provided.
    int extractAll(const std::filesystem::path& outDir,
                    const std::function<void(int, const std::string&)>& progress = {}) const;

private:
    struct DirRecord;
    // Reads one directory: avatarBlock is its first disc block; a
    // directory's blocks are chained by DirectoryHeader.dh_NextBlock,
    // which is an index RELATIVE to the directory's own start (3dt
    // FSWalker: base + dirBlockSize * next_block), NOT an absolute disc
    // block. Records within a block run from dh_FirstEntryOffset to
    // dh_FirstFreeByte.
    void readDirectory(uint32_t avatarBlock, uint32_t dirBlockSize, uint32_t dirBlockCount,
                        const std::string& parentPath,
                        const std::function<void(const Entry&)>& cb) const;
    std::vector<uint8_t> readBlocks(uint32_t block, uint32_t count) const;

    // Sector-abstracted read of `len` user-data bytes at absolute user-data
    // offset (block * blockSize). Hides the raw/cooked sector layout. Bytes
    // past the image end are zero-filled; returns the count actually read.
    size_t readUserBytes(uint64_t userOffset, uint8_t* dst, size_t len) const;
    bool blockAvailable(uint32_t block) const;

    std::vector<uint8_t> image_;   // whole image (or decompressed CHD user data)
    std::string volumeName_;
    uint32_t blockSize_ = 2048;
    uint32_t rootBlock_ = 0;
    uint32_t rootBlockCount_ = 1;
    uint32_t rootBlockSize_ = 2048;

    // Raw-sector geometry: for 2352-byte images each 2048-byte logical
    // block sits at (sectorSize*n + sectorHeader). For cooked 2048 images
    // sectorSize == 2048 and sectorHeader == 0.
    uint32_t sectorSize_ = 2048;
    uint32_t sectorHeader_ = 0;
};

// Crystal Dynamics "bigfile" archive (Gex: GXdata/bigfile). Big-endian
// layout, verified against the real 331 MB file:
//   u32 entryCount
//   u32 reserved[3]
//   entryCount x { u32 id (filename hash), u32 size, u32 offset }
//   ... payload, each entry starting on a 2048-byte (disc sector) boundary
// Each entry's offset equals the previous entry's end rounded up to 2048,
// which is what confirms the field order. Names aren't stored — only
// hashes — so extracted files are numbered and typed by content sniffing.
class BigFile {
public:
    struct Entry {
        uint32_t id = 0;
        uint32_t size = 0;
        uint32_t offset = 0;
    };

    static BigFile openFromFile(const std::filesystem::path& path);

    const std::vector<Entry>& entries() const { return entries_; }
    // Reads one entry's bytes from the archive on demand (the archive is
    // far too large to hold in memory).
    std::vector<uint8_t> read(size_t index) const;
    // Extracts every entry to outDir as entry_<n>_<id>.bin.
    int extractAll(const std::filesystem::path& outDir,
                    const std::function<void(int, const std::string&)>& progress = {}) const;

private:
    std::filesystem::path path_;
    std::vector<Entry> entries_;
};

} // namespace m2disc
