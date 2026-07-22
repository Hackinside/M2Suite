#include "m2disasm/Elf.h"

#include <cstdio>
#include <fstream>

#include "m2core/Error.h"

#include "../third_party/CommonDefs.h" // u32/u64 types ppcd.h depends on
#include "../third_party/ppcd.h"

namespace m2disasm {

using m2core::FormatError;

namespace {
uint16_t u16be(const uint8_t* p) {
    return uint16_t((p[0] << 8) | p[1]);
}
uint32_t u32be(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// 3DO's ELF dialect marks on-disc LZSS-compressed sections with sh_flags
// bit 0x8 (SHF_COMPRESS, ws_root loader/elf.h); drivers like
// bdavideo.driver ship .text this way, which is why naive reads produced
// byte-shifted garbage. This is a port of the OS compression folio's
// decoder (ws_root src/folios/compression/decompress.c + lzss.h):
// 4096-byte window with write position starting at 1, MSB-first control
// bits (set = literal, clear = match), 16-bit match words split 12/4 into
// window position and length, length biased by BREAK_EVEN (2) and the copy
// loop inclusive (so 3..18 bytes), window position 0 = end of stream.
constexpr uint32_t kShfCompress3do = 0x8;

std::vector<uint8_t> lzssDecompress(const uint8_t* src, size_t srcSize, size_t maxOut) {
    std::vector<uint8_t> out;
    uint8_t window[4096] = {};
    uint32_t pos = 1;
    auto put = [&](uint8_t b) {
        if (out.size() < maxOut) {
            out.push_back(b);
        }
        window[pos] = b;
        pos = (pos + 1) & 4095;
    };
    size_t i = 0;
    while (i < srcSize) {
        uint8_t control = src[i++];
        for (int bit = 7; bit >= 0; --bit) {
            if (control & (1u << bit)) {
                if (i >= srcSize) return out;
                put(src[i++]);
            } else {
                if (i + 1 >= srcSize) return out;
                uint32_t matchWord = (uint32_t(src[i]) << 8) | src[i + 1];
                i += 2;
                uint32_t matchPos = matchWord >> 4;
                if (matchPos == 0) return out; // END_OF_STREAM
                uint32_t matchLen = (matchWord & 15) + 2;
                for (uint32_t k = matchPos; k <= matchPos + matchLen; ++k) {
                    put(window[k & 4095]);
                }
            }
        }
    }
    return out;
}
} // namespace

Elf Elf::load(const uint8_t* data, size_t size) {
    if (size < 52 || data[0] != 0x7F || data[1] != 'E' || data[2] != 'L' || data[3] != 'F') {
        throw FormatError("not an ELF file");
    }
    if (data[4] != 1) { // EI_CLASS: 1 = 32-bit
        throw FormatError("only ELF32 supported (M2 executables are ELF32)");
    }
    Elf elf;
    elf.data_.assign(data, data + size);
    elf.bigEndian_ = (data[5] == 2); // EI_DATA: 2 = big-endian
    if (!elf.bigEndian_) {
        throw FormatError("little-endian ELF unexpected for M2 (PowerPC BE)");
    }

    const uint8_t* d = elf.data_.data();
    elf.machine_ = u16be(d + 18);
    elf.entry_ = u32be(d + 24);
    uint32_t shoff = u32be(d + 32);
    uint16_t shentsize = u16be(d + 46);
    uint16_t shnum = u16be(d + 48);
    uint16_t shstrndx = u16be(d + 50);

    if (shoff == 0 || shnum == 0 || shentsize < 40 ||
        size_t(shoff) + size_t(shnum) * shentsize > size) {
        return elf; // no/damaged section table — header info still useful
    }

    // Section-name string table.
    const uint8_t* shstr = nullptr;
    size_t shstrSize = 0;
    if (shstrndx < shnum) {
        const uint8_t* sh = d + shoff + size_t(shstrndx) * shentsize;
        uint32_t strOff = u32be(sh + 16);
        uint32_t strSize = u32be(sh + 20);
        if (size_t(strOff) + strSize <= size) {
            shstr = d + strOff;
            shstrSize = strSize;
        }
    }

    for (uint16_t i = 0; i < shnum; ++i) {
        const uint8_t* sh = d + shoff + size_t(i) * shentsize;
        Section s;
        uint32_t nameOff = u32be(sh);
        s.type = u32be(sh + 4);
        s.flags = u32be(sh + 8);
        s.addr = u32be(sh + 12);
        s.offset = u32be(sh + 16);
        s.size = u32be(sh + 20);
        if (shstr && nameOff < shstrSize) {
            const char* nm = reinterpret_cast<const char*>(shstr + nameOff);
            size_t maxLen = shstrSize - nameOff;
            size_t len = 0;
            while (len < maxLen && nm[len]) ++len;
            s.name.assign(nm, len);
        }
        elf.sections_.push_back(std::move(s));
    }

    // Inflate 3DO SHF_COMPRESS sections in place: append the decompressed
    // bytes to data_ and repoint the section, so every downstream consumer
    // (disassembly, symbols, strings) sees plain data.
    for (Section& s : elf.sections_) {
        if (!(s.flags & kShfCompress3do) || s.size == 0 ||
            size_t(s.offset) + s.size > elf.data_.size()) {
            continue;
        }
        if (s.type != 1 && s.type != 2 && s.type != 3) { // PROGBITS/SYMTAB/STRTAB
            continue;
        }
        std::vector<uint8_t> blob =
            lzssDecompress(elf.data_.data() + s.offset, s.size, 64u << 20);
        if (blob.empty()) {
            continue;
        }
        s.offset = uint32_t(elf.data_.size());
        s.size = uint32_t(blob.size());
        s.flags &= ~kShfCompress3do;
        s.wasCompressed = true;
        elf.data_.insert(elf.data_.end(), blob.begin(), blob.end());
    }

    elf.parseSymbols();
    elf.parseImports();
    elf.parseBinHeader();
    return elf;
}

const Elf::Section* Elf::findSection(const std::string& name) const {
    for (const auto& s : sections_) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

void Elf::parseSymbols() {
    const Section* symtab = findSection(".symtab");
    const Section* strtab = findSection(".strtab");
    if (!symtab || !strtab) {
        return;
    }
    if (size_t(symtab->offset) + symtab->size > data_.size() ||
        size_t(strtab->offset) + strtab->size > data_.size()) {
        return;
    }
    const uint8_t* base = data_.data() + symtab->offset;
    const char* strs = reinterpret_cast<const char*>(data_.data() + strtab->offset);
    size_t strSize = strtab->size;
    size_t count = symtab->size / 16; // ELF32 symbol entry = 16 bytes

    for (size_t i = 0; i < count; ++i) {
        const uint8_t* e = base + i * 16;
        uint32_t nameOff = u32be(e);
        uint32_t value = u32be(e + 4);
        uint32_t size = u32be(e + 8);
        uint8_t info = e[12];
        uint8_t stype = info & 0xF; // STT_*
        if (nameOff == 0 || nameOff >= strSize) {
            continue;
        }
        size_t maxLen = strSize - nameOff;
        size_t len = 0;
        while (len < maxLen && strs[nameOff + len]) ++len;
        if (len == 0) {
            continue;
        }
        Symbol sym;
        sym.name.assign(strs + nameOff, len);
        sym.value = value;
        sym.size = size;
        sym.type = stype;
        // FUNC symbols (2) and NOTYPE labels (0) with a nonzero address get
        // an address→name mapping for labels and branch resolution.
        if ((stype == 2 || stype == 0) && value != 0) {
            funcByAddr_.emplace(value, sym.name);
        }
        symbols_.push_back(std::move(sym));
    }
}

void Elf::parseImports() {
    // .imp3do is an ELF note (SHT_NOTE) whose descriptor is an ELF_Imp3DO
    // (loader/elf_3do.h): after the 16-byte note header {namesz=4, descsz,
    // type='i', name='SKH\0'} comes numImports, then that many 12-byte
    // ELF_ImportRec {nameOffset, libraryCode, version, revision, flags,
    // pad}. nameOffset is a byte offset from the start of the descriptor
    // into a trailing string pool. Falls back to a raw string scan if the
    // note header doesn't match.
    const Section* imp = findSection(".imp3do");
    if (!imp || imp->size < 20 || size_t(imp->offset) + imp->size > data_.size()) {
        return;
    }
    const uint8_t* sec = data_.data() + imp->offset;
    uint32_t noteType = u32be(sec + 8);
    uint32_t noteName = u32be(sec + 12);

    bool parsed = false;
    if (noteType == uint32_t('i') && noteName == 0x534b4800 /* 'SKH\0' */) {
        const uint8_t* desc = sec + 16;
        size_t descSize = imp->size - 16;
        if (descSize >= 4) {
            uint32_t numImports = u32be(desc);
            // Guard against corrupt counts.
            if (numImports <= 256 && 4 + size_t(numImports) * 12 <= descSize) {
                parsed = true;
                // The name string pool follows the fixed-size record array;
                // each record's nameOffset is relative to that pool base
                // (verified against Polystar/3DOM2VIZ/fz35s launchers).
                size_t poolBase = 4 + size_t(numImports) * 12;
                for (uint32_t i = 0; i < numImports; ++i) {
                    const uint8_t* rec = desc + 4 + size_t(i) * 12;
                    Import imp3do;
                    uint32_t nameOff = u32be(rec);
                    imp3do.libraryCode = u32be(rec + 4);
                    imp3do.version = rec[8];
                    imp3do.revision = rec[9];
                    imp3do.flags = rec[10];
                    size_t nameAt = poolBase + nameOff;
                    if (nameAt < descSize) {
                        const char* nm = reinterpret_cast<const char*>(desc + nameAt);
                        size_t maxLen = descSize - nameAt;
                        size_t len = 0;
                        while (len < maxLen && nm[len]) ++len;
                        imp3do.name.assign(nm, len);
                    }
                    imports_.push_back(imp3do.name);
                    importRecords_.push_back(std::move(imp3do));
                }
            }
        }
    }

    if (!parsed) {
        const char* p = reinterpret_cast<const char*>(sec);
        std::string run;
        for (uint32_t i = 0; i < imp->size; ++i) {
            char c = p[i];
            if (c >= 32 && c <= 126) {
                run += c;
            } else {
                if (run.size() >= 3) {
                    imports_.push_back(run);
                }
                run.clear();
            }
        }
        if (run.size() >= 3) {
            imports_.push_back(run);
        }
    }
}

void Elf::parseBinHeader() {
    // .hdr3do descriptor is a _3DOBinHeader (loader/header3do.h). It's an
    // ELF note too; the ItemNode prefix varies, so locate the fields by the
    // fixed tail layout that the loader relies on: flags/osver/osrev at a
    // known offset then Name[32]. We scan for the note descriptor and read
    // the documented fields relative to it.
    const Section* hdr = nullptr;
    for (const auto& s : sections_) {
        if (s.name == ".hdr3do" && s.size >= 16) {
            hdr = &s;
            break;
        }
    }
    if (!hdr || size_t(hdr->offset) + hdr->size > data_.size()) {
        return;
    }
    const uint8_t* sec = data_.data() + hdr->offset;
    const uint8_t* desc = sec;
    size_t descSize = hdr->size;
    // Skip the ELF note header when present (name 'SKH\0').
    if (hdr->size >= 16 && u32be(sec + 12) == 0x534b4800) {
        desc = sec + 16;
        descSize = hdr->size - 16;
    }
    // _3DOBinHeader: ItemNode (n_ prefix) then _3DO_Flags(1), OS_Version(1),
    // OS_Revision(1), Reserved0(1), Stack(4), Reserved1[3], MaxUSecs(4),
    // Reserved2(4), Name[32], Time(4). The ItemNode is 36 bytes in the M2
    // kernel (nodes.h: 2 ptrs + 4 + n_Size + n_Name + 4 + n_Item + n_Owner
    // + n_Reserved1). Read defensively.
    constexpr size_t kItemNode = 36;
    if (descSize < kItemNode + 20 + 32) {
        return;
    }
    const uint8_t* h = desc + kItemNode;
    binHeader_.valid = true;
    binHeader_.flags = h[0];
    binHeader_.osVersion = h[1];
    binHeader_.osRevision = h[2];
    binHeader_.stack = u32be(h + 4);
    binHeader_.maxUSecs = u32be(h + 20);
    const char* nm = reinterpret_cast<const char*>(h + 28);
    size_t maxLen = descSize - kItemNode - 28;
    size_t len = 0;
    while (len < maxLen && len < 32 && nm[len]) ++len;
    binHeader_.name.assign(nm, len);
    if (kItemNode + 28 + 32 + 4 <= descSize) {
        binHeader_.time = u32be(h + 60);
    }
}

Elf Elf::loadFromFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw FormatError("could not open file: " + path.string());
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
    return load(bytes.data(), bytes.size());
}

std::string Elf::disassembleSection(const Section& section, size_t maxInstructions) const {
    std::string out;
    if (section.offset + section.size > data_.size() || section.size == 0) {
        return out;
    }
    const uint8_t* base = data_.data() + section.offset;
    size_t count = section.size / 4;
    bool truncated = false;
    if (maxInstructions && count > maxInstructions) {
        count = maxInstructions;
        truncated = true;
    }
    out.reserve(count * 48);

    char line[256];
    PPCD_CB cb{};
    for (size_t i = 0; i < count; ++i) {
        uint32_t addr = uint32_t(section.addr + i * 4);

        // Emit a function/label line when a symbol starts at this address.
        auto label = funcByAddr_.find(addr);
        if (label != funcByAddr_.end()) {
            out += "\n";
            out += label->second;
            out += ":\n";
        }

        uint32_t word = u32be(base + i * 4);
        cb.pc = addr;
        cb.instr = word;
        PPCDisasm(&cb);

        // Data vs. code: ppcd flags words it can't decode as ILLEGAL. In a
        // real .text these are interleaved jump tables, constant pools, and
        // padding — emit them as `.long` data rather than inventing a bogus
        // instruction (which is what produced the "raw hex opcode" noise on
        // driver files like bdavideo.driver). A printable ASCII quad is
        // annotated so embedded ids in the data are visible.
        if (cb.iclass & PPC_DISA_ILLEGAL) {
            char ascii[5] = {char(word >> 24), char(word >> 16), char(word >> 8), char(word), 0};
            bool printable = word != 0;
            for (int k = 0; k < 4 && printable; ++k) {
                if (ascii[k] < 32 || ascii[k] > 126) {
                    printable = false;
                }
            }
            if (printable) {
                std::snprintf(line, sizeof(line), "%08x:  %08x  .long      0x%08x  ; '%s'\n",
                               addr, word, word, ascii);
            } else {
                std::snprintf(line, sizeof(line), "%08x:  %08x  .long      0x%08x\n", addr,
                               word, word);
            }
            out += line;
            continue;
        }

        // Resolve branch targets to a symbol name when we have one.
        std::string comment;
        if (cb.iclass & PPC_DISA_BRANCH) {
            auto target = funcByAddr_.find(uint32_t(cb.target));
            if (target != funcByAddr_.end()) {
                comment = "  ; -> " + target->second;
            }
        }

        std::snprintf(line, sizeof(line), "%08x:  %08x  %-10s %s%s\n", addr, word, cb.mnemonic,
                       cb.operands, comment.c_str());
        out += line;
    }
    if (truncated) {
        out += "...  (truncated for display — use Export Selected for the full listing)\n";
    }
    return out;
}

namespace {
// Does a printable run look like real text rather than a random slice of a
// binary table that happens to fall in the ASCII range? Compiled data is
// full of short high-entropy runs ("L*_+", ";'5A<,") — require mostly
// word-like characters and at least one letter to keep the dump readable.
bool looksLikeText(const std::string& run) {
    size_t wordish = 0;
    bool hasLetter = false;
    for (char c : run) {
        bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        hasLetter = hasLetter || letter;
        if (letter || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '.' || c == '/' ||
            c == '-' || c == ':' || c == '%') {
            ++wordish;
        }
    }
    return hasLetter && wordish * 100 >= run.size() * 70;
}
} // namespace

std::string Elf::extractStrings(size_t minLength) const {
    std::string out;
    char header[64];
    for (const Section& s : sections_) {
        if (s.type != 1 || s.size == 0) { // SHT_PROGBITS with content
            continue;
        }
        if (s.name != ".data" && s.name != ".rodata" && s.name != ".strtab") {
            continue;
        }
        if (size_t(s.offset) + s.size > data_.size()) {
            continue;
        }
        std::snprintf(header, sizeof(header), "\n; ===== strings in %s =====\n", s.name.c_str());
        out += header;
        const uint8_t* d = data_.data() + s.offset;
        std::string run;
        uint32_t runStart = 0;
        for (uint32_t i = 0; i <= s.size; ++i) {
            char c = (i < s.size) ? char(d[i]) : '\0'; // flush the final run
            if (c >= 32 && c <= 126) {
                if (run.empty()) {
                    runStart = s.addr + i;
                }
                run += c;
            } else {
                if (run.size() >= minLength && looksLikeText(run)) {
                    char addrbuf[16];
                    std::snprintf(addrbuf, sizeof(addrbuf), "%08x: ", runStart);
                    out += addrbuf;
                    out += run;
                    out += '\n';
                }
                run.clear();
            }
        }
    }
    return out;
}

std::string Elf::disassembleAll(size_t maxInstructionsPerSection) const {
    std::string out;
    for (const Section& s : sections_) {
        if (!s.executable() || !s.progbits() || s.size == 0) {
            continue;
        }
        out += "\n; ======== section " + s.name + " ========\n";
        out += disassembleSection(s, maxInstructionsPerSection);
    }
    return out;
}

} // namespace m2disasm
