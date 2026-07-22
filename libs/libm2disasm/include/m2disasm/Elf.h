#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace m2disasm {

// Minimal ELF32 big-endian reader for 3DO M2 executables (PowerPC 602,
// EM_PPC). M2 discs carry dozens of these (.ELF magic, extensionless).
// Disassembly is provided by the bundled ppcd PowerPC disassembler
// (third_party/ppcd.*, CC0 public domain, by org/ogamespec).
class Elf {
public:
    struct Section {
        std::string name;
        uint32_t type = 0;
        uint32_t flags = 0;
        uint32_t addr = 0;
        uint32_t offset = 0;
        uint32_t size = 0;
        // 3DO extension: SHF_COMPRESS (0x8, loader/elf.h) marks the section
        // as LZSS-compressed on disc. load() inflates such sections in
        // place (offset/size then refer to the decompressed bytes) and sets
        // this flag for display.
        bool wasCompressed = false;
        bool executable() const { return (flags & 0x4) != 0; } // SHF_EXECINSTR
        bool progbits() const { return type == 1; }             // SHT_PROGBITS
    };

    static Elf loadFromFile(const std::filesystem::path& path);
    static Elf load(const uint8_t* data, size_t size);

    struct Symbol {
        std::string name;
        uint32_t value = 0;
        uint32_t size = 0;
        uint8_t type = 0; // 1 = OBJECT, 2 = FUNC
    };

    // One imported DLL/folio from the .imp3do section (ELF_ImportRec in the
    // SDK's loader/elf_3do.h). Modules import shared OS folios by library
    // code + version/revision, with loader flags (IMPORT_NOW etc).
    struct Import {
        std::string name;
        uint32_t libraryCode = 0;
        uint8_t version = 0;
        uint8_t revision = 0;
        uint8_t flags = 0; // IMPORT_NOW 0x01 / REIMPORT_ALLOWED 0x02 / IMPORT_REQUIRED 0x04
    };

    // The 3DO application header (.hdr3do → _3DOBinHeader in the SDK's
    // loader/header3do.h). Carries the module name, target OS version, stack
    // size, scheduling quantum, and privilege flags.
    struct BinHeader {
        bool valid = false;
        std::string name;
        uint8_t flags = 0;
        uint8_t osVersion = 0;
        uint8_t osRevision = 0;
        uint32_t stack = 0;
        uint32_t maxUSecs = 0;
        uint32_t time = 0; // seconds since 1993-01-01 00:00 GMT
    };

    uint16_t machine() const { return machine_; } // 20 = EM_PPC, 17 = EM_PPC_OLD
    // M2 SDK executables were built with the Diab compiler, which stamps
    // the pre-standardization PowerPC machine id 17 (EM_PPC_OLD).
    bool isPowerPC() const { return machine_ == 20 || machine_ == 17; }
    uint32_t entryPoint() const { return entry_; }
    bool isBigEndian() const { return bigEndian_; }
    const std::vector<Section>& sections() const { return sections_; }
    const std::vector<Symbol>& symbols() const { return symbols_; }
    const std::vector<std::string>& imports() const { return imports_; }
    const std::vector<Import>& importRecords() const { return importRecords_; }
    const BinHeader& binHeader() const { return binHeader_; }

    // Raw (post-decompression) file image; section .offset indexes into it.
    // Exposed so companion tools (e.g. Pseudocode) can read instruction
    // words by address without re-reading the file.
    const std::vector<uint8_t>& rawData() const { return data_; }

    // Disassembles one executable section to a symbol-annotated listing:
    // function labels ("<name>:"), and branch targets resolved to symbol
    // names as trailing comments. maxInstructions caps output for the
    // viewer; pass 0 for unlimited (export).
    std::string disassembleSection(const Section& section,
                                    size_t maxInstructions = 0) const;

    // Convenience: full-module listing of every executable section.
    std::string disassembleAll(size_t maxInstructionsPerSection = 0) const;

    // Extracts printable ASCII strings (>= minLength) from the data/rodata
    // sections — useful for locating messages, format strings, and asset
    // names without a full decompile. Each entry is "addr: text".
    std::string extractStrings(size_t minLength = 4) const;

private:
    const Section* findSection(const std::string& name) const;
    void parseSymbols();
    void parseImports();
    void parseBinHeader();

    std::vector<uint8_t> data_;
    uint16_t machine_ = 0;
    uint32_t entry_ = 0;
    bool bigEndian_ = true;
    std::vector<Section> sections_;
    std::vector<Symbol> symbols_;
    std::map<uint32_t, std::string> funcByAddr_; // FUNC/label symbols by address
    std::vector<std::string> imports_;           // .imp3do folio import names (legacy)
    std::vector<Import> importRecords_;          // structured .imp3do records
    BinHeader binHeader_;                        // .hdr3do _3DOBinHeader
};

} // namespace m2disasm
