// Parses a real M2 ELF (an EventBroker driverlet from imsaM2) and checks
// the header, sections, and that the PowerPC disassembly contains
// plausible function scaffolding (mflr/blr appear in virtually all
// compiled PPC code).
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#include "m2disasm/Elf.h"
#include "m2disasm/Pseudocode.h"

// Prints the 3DO application header (.hdr3do) and structured imports
// (.imp3do) for one M2 executable — the shared front-matter used by the
// launcher-comparison analysis.
static void printModuleInfo(const m2disasm::Elf& elf, const char* label) {
    std::printf("======== %s ========\n", label);
    const auto& h = elf.binHeader();
    if (h.valid) {
        std::printf("  3DO header: name='%s' osver=%u.%u stack=%u maxUSecs=%u flags=0x%02x\n",
                     h.name.c_str(), h.osVersion, h.osRevision, h.stack, h.maxUSecs, h.flags);
    }
    std::printf("  entry=0x%08x  sections=%zu  symbols=%zu\n", elf.entryPoint(),
                 elf.sections().size(), elf.symbols().size());
    std::printf("  imported folios (%zu):\n", elf.importRecords().size());
    for (const auto& imp : elf.importRecords()) {
        std::printf("    %-20s v%u.%u  code=0x%08x  flags=0x%02x%s\n", imp.name.c_str(),
                     imp.version, imp.revision, imp.libraryCode, imp.flags,
                     (imp.flags & 0x01) ? " [IMPORT_NOW]" : "");
    }
}

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n", #cond, __FILE__, \
                          __LINE__);                                            \
            std::exit(1);                                                       \
        }                                                                       \
    } while (0)

using namespace m2disasm;

int main(int argc, char** argv) {
    // --info <elf>...            : print 3DO header + imports for each file
    // --pseudo <elf> [out.c]     : lift to ANSI-C pseudocode
    if (argc > 2 && std::string(argv[1]) == "--info") {
        for (int i = 2; i < argc; ++i) {
            try {
                m2disasm::Elf elf = m2disasm::Elf::loadFromFile(argv[i]);
                printModuleInfo(elf, argv[i]);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "%s: %s\n", argv[i], e.what());
            }
        }
        return 0;
    }
    if (argc > 2 && std::string(argv[1]) == "--pseudo") {
        m2disasm::Elf elf = m2disasm::Elf::loadFromFile(argv[2]);
        m2disasm::Pseudocode pc(elf);
        std::string code = pc.liftAll(512);
        if (argc > 3) {
            FILE* f = std::fopen(argv[3], "wb");
            if (f) {
                std::fwrite(code.data(), 1, code.size(), f);
                std::fclose(f);
            }
            std::printf("wrote %zu bytes of pseudocode to %s\n", code.size(), argv[3]);
        } else {
            std::fwrite(code.data(), 1, code.size(), stdout);
        }
        return 0;
    }

    const char* path = (argc > 1) ? argv[1] : "tests/fixtures/sample.elf";
    try {
        Elf elf = Elf::loadFromFile(path);
        CHECK(elf.isBigEndian());
        CHECK(elf.isPowerPC()); // EM_PPC or Diab's EM_PPC_OLD (17)
        CHECK(!elf.sections().empty());

        std::string listing = elf.disassembleAll(0);
        CHECK(!listing.empty());
        CHECK(listing.find("mflr") != std::string::npos ||
               listing.find("blr") != std::string::npos);
        std::printf("libm2disasm_tests: %zu sections, %zu symbols, %zu imports, %zu bytes\n",
                     elf.sections().size(), elf.symbols().size(), elf.imports().size(),
                     listing.size());

        // Optional dump: argv[2] writes the annotated listing + strings.
        if (argc > 2) {
            std::string full = listing + "\n" + elf.extractStrings();
            FILE* f = std::fopen(argv[2], "wb");
            if (f) {
                std::fwrite(full.data(), 1, full.size(), f);
                std::fclose(f);
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "UNCAUGHT EXCEPTION: %s\n", e.what());
        return 1;
    }
    std::printf("libm2disasm_tests: all checks passed\n");
    return 0;
}
