// Opens a real 3DO disc image and either lists or extracts it. Used both as
// a smoke test and a CLI utility during development.
//   test_opera_disc <image>                 — print volume + file listing
//   test_opera_disc <image> --extract <dir> — extract all files
#include <cstdio>
#include <exception>
#include <string>

#include "m2disc/OperaDisc.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <image> [--extract <dir>]\n", argv[0]);
        std::fprintf(stderr, "       %s --bigfile <bigfile> [--extract <dir>]\n", argv[0]);
        return 2;
    }
    try {
        if (std::string(argv[1]) == "--bigfile" && argc >= 3) {
            m2disc::BigFile bf = m2disc::BigFile::openFromFile(argv[2]);
            std::printf("bigfile entries: %zu\n", bf.entries().size());
            for (size_t i = 0; i < bf.entries().size() && i < 8; ++i) {
                const auto& e = bf.entries()[i];
                std::printf("  [%zu] id=%08x size=%u offset=%u\n", i, e.id, e.size, e.offset);
            }
            if (argc >= 5 && std::string(argv[3]) == "--extract") {
                int n = bf.extractAll(argv[4], [](int c, const std::string& nm) {
                    if (c % 25 == 0) std::printf("  ...%d (%s)\n", c, nm.c_str());
                });
                std::printf("extracted %d entries to %s\n", n, argv[4]);
            }
            return 0;
        }
        m2disc::OperaDisc disc = m2disc::OperaDisc::open(argv[1]);
        std::printf("volume: '%s'  blockSize=%u\n", disc.volumeName().c_str(),
                     disc.blockSize());

        if (argc >= 4 && std::string(argv[2]) == "--extract") {
            int n = disc.extractAll(argv[3], [](int count, const std::string& path) {
                if (count % 50 == 0) {
                    std::printf("  ...%d files (%s)\n", count, path.c_str());
                }
            });
            std::printf("extracted %d files to %s\n", n, argv[3]);
        } else {
            int files = 0, dirs = 0;
            disc.list([&](const m2disc::OperaDisc::Entry& e) {
                if (e.isDirectory) {
                    ++dirs;
                } else {
                    ++files;
                }
                if (files + dirs <= 40) {
                    std::printf("  %s%s  (%u bytes)\n", e.path.c_str(),
                                 e.isDirectory ? "/" : "", e.byteCount);
                }
            });
            std::printf("total: %d files, %d directories\n", files, dirs);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR: %s\n", e.what());
        return 1;
    }
    return 0;
}
