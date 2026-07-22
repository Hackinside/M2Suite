// Decodes the real-world fixture tests/fixtures/Message.cel (a coded-8bpp,
// packed, 32-entry-PLUT cel dumped from an M2 demo disc) and checks the
// parsed header and decoded image against the values verified by hex
// inspection when the fixture was catalogued.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "m2cel/Anim.h"
#include "m2cel/Cel.h"
#include "m2cel/Imag.h"

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n", #cond, __FILE__, \
                          __LINE__);                                            \
            std::exit(1);                                                       \
        }                                                                       \
    } while (0)

using namespace m2cel;

static int runTests(int argc, char** argv);

int main(int argc, char** argv) {
    try {
        return runTests(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "UNCAUGHT EXCEPTION: %s\n", e.what());
        return 1;
    }
}

static int runTests(int argc, char** argv) {
    // Fixture path comes from CMake (first arg); fall back to a relative
    // path for manual runs from the repo root.
    const char* path = (argc > 1) ? argv[1] : "tests/fixtures/Message.cel";

    Cel cel = Cel::loadFromFile(path);

    // Byte-exact header values apply only to the canonical fixture; any
    // other .cel path (e.g. batch sweeps over game data) gets the generic
    // decode checks below.
    bool isCanonicalFixture = std::strstr(path, "Message.cel") != nullptr;
    if (isCanonicalFixture) {
        CHECK(cel.ccb().width == 206);
        CHECK(cel.ccb().height == 72);
        CHECK(cel.ccb().bitsPerPixel() == 8);
        CHECK(!cel.ccb().isUncoded()); // coded (PLUT-indexed)
        CHECK(cel.ccb().isPacked());
        CHECK(cel.plut().size() == 32);
        CHECK(cel.pixelData().size() == 0x1410 - 8);
    }

    auto rgba = cel.decodeToRgba();
    CHECK(rgba.size() == size_t(cel.ccb().width) * cel.ccb().height);

    // Sanity: the decode must produce drawn pixels with some color variety
    // — an all-transparent or single-color result would indicate a broken
    // row/packet walk. (This fixture happens to be a full-rectangle banner
    // with no transparent packets, so transparency is not asserted.)
    size_t opaque = 0, transparent = 0;
    bool seen[256] = {};
    size_t distinctGrays = 0;
    for (const auto& px : rgba) {
        if (px.a == 0) {
            ++transparent;
        } else {
            ++opaque;
            if (!seen[px.r]) {
                seen[px.r] = true;
                ++distinctGrays;
            }
        }
    }
    if (isCanonicalFixture) {
        CHECK(opaque > 0);
        CHECK(distinctGrays > 4); // 32-entry ramp PLUT: expect real variety
    }
    std::printf("libm2cel_tests: %zu opaque / %zu transparent, %zu distinct levels\n",
                 opaque, transparent, distinctGrays);

    // Optional PPM dump for eyeball verification: pass an output path as
    // the second argument.
    if (argc > 2 && argv[2][0] != '\0') {
        FILE* f = std::fopen(argv[2], "wb");
        CHECK(f != nullptr);
        std::fprintf(f, "P6\n%u %u\n255\n", cel.ccb().width, cel.ccb().height);
        for (const auto& px : rgba) {
            unsigned char rgb[3] = {px.r, px.g, px.b};
            std::fwrite(rgb, 1, 3, f);
        }
        std::fclose(f);
        std::printf("libm2cel_tests: wrote PPM dump to %s\n", argv[2]);
    }
    // Optional real-game fixtures: argv[3] = ANIM file, argv[4] = IMAG file
    // (from strahl). Decode fully and sanity-check dimensions. Empty
    // strings skip a slot (argv[2] stays reserved for a PPM dump path).
    if (argc > 3 && argv[3][0] != '\0') {
        Anim anim = Anim::loadFromFile(argv[3]);
        CHECK(!anim.frames().empty());
        size_t totalOpaque = 0;
        for (size_t i = 0; i < anim.frames().size(); ++i) {
            const auto& ccb = anim.frames()[i].ccb;
            auto frame = anim.decodeFrame(i);
            CHECK(frame.size() == size_t(ccb.width) * ccb.height);
            for (const auto& px : frame) {
                if (px.a != 0) ++totalOpaque;
            }
        }
        std::printf("libm2cel_tests: ANIM decoded %zu frames (header numFrames=%u), "
                     "%zu total opaque px\n",
                     anim.frames().size(), anim.numFrames, totalOpaque);
        // Dump frame 0 to PPM if a path is given as argv[5].
        if (argc > 5 && argv[5][0] != '\0') {
            const auto& ccb = anim.frames()[0].ccb;
            auto frame = anim.decodeFrame(0);
            FILE* f = std::fopen(argv[5], "wb");
            if (f) {
                std::fprintf(f, "P6\n%u %u\n255\n", ccb.width, ccb.height);
                for (const auto& px : frame) {
                    unsigned char rgb[3] = {px.r, px.g, px.b};
                    std::fwrite(rgb, 1, 3, f);
                }
                std::fclose(f);
            }
        }
    }
    if (argc > 4 && argv[4][0] != '\0') {
        Imag img = Imag::loadFromFile(argv[4]);
        auto rgba = img.decodeToRgba();
        CHECK(rgba.size() == size_t(img.width) * img.height);
        std::printf("libm2cel_tests: IMAG decoded %ux%u (pixelOrder=%u)\n", img.width,
                     img.height, img.pixelOrder);
        if (argc > 5 && argv[5][0] != '\0') {
            FILE* f = std::fopen(argv[5], "wb");
            CHECK(f != nullptr);
            std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
            for (const auto& px : rgba) {
                unsigned char rgb[3] = {px.r, px.g, px.b};
                std::fwrite(rgb, 1, 3, f);
            }
            std::fclose(f);
        }
    }

    std::printf("libm2cel_tests: all checks passed\n");
    return 0;
}
