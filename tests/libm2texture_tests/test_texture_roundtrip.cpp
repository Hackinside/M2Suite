// Builds a small synthetic UTF/M2TX file byte-for-byte (matching the
// on-disk layout verified against reference/sdk-mercury/txtlib/src/
// M2TXiff.c), then parses it with m2texture::Texture and checks the result.
// This stands in for a real-world .utf fixture until one is sourced from
// 3-1/Examples/M2_3.1 (see reference/README.md) or produced via a classic
// Mac emulator running the original ppmtoutf tool.
#include <cstdio>
#include <cstdlib>
#include <exception>

#include <vector>

#include "m2core/ByteStream.h"
#include "m2core/Iff.h"
#include "m2texture/Texture.h"
#include "m2texture/TextureEncoder.h"

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n", #cond, __FILE__, \
                          __LINE__);                                            \
            std::exit(1);                                                       \
        }                                                                       \
    } while (0)

using namespace m2texture;
using namespace m2core;

namespace {
constexpr uint32_t ID_TXTR = makeId('T', 'X', 'T', 'R');
constexpr uint32_t ID_M2TX = makeId('M', '2', 'T', 'X');
constexpr uint32_t ID_M2PI = makeId('M', '2', 'P', 'I');
constexpr uint32_t ID_M2CI = makeId('M', '2', 'C', 'I');
constexpr uint32_t ID_M2TD = makeId('M', '2', 'T', 'D');

std::vector<uint8_t> buildSynthetic2x2Utf() {
    // M2TX header chunk: 16 bytes.
    ByteWriter m2tx;
    m2tx.writeU32BE(0);                          // reserved
    m2tx.writeU32BE(HeaderFlags::HasPIP);         // flags: has palette, not compressed
    m2tx.writeU16BE(2);                           // MinXSize
    m2tx.writeU16BE(2);                           // MinYSize
    m2tx.writeU16BE(TexelFlags::HasColor | 8);     // TexFormat: has color, 8-bit index
    m2tx.writeU8(1);                               // NumLOD
    m2tx.writeU8(0);                               // reserved2

    // M2PI (PIP) chunk: indexOffset(4) + reserved(4) + 2 color entries.
    ByteWriter m2pi;
    m2pi.writeU32BE(0);
    m2pi.writeU32BE(0);
    Rgba8 red{255, 0, 0, 254, false};
    Rgba8 green{0, 255, 0, 254, false};
    m2pi.writeU32BE(encodeM2TXColor(red));
    m2pi.writeU32BE(encodeM2TXColor(green));

    // M2CI (DCI) chunk: 24 bytes.
    ByteWriter m2ci;
    m2ci.writeU16BE(TexelFlags::HasColor | 8);
    m2ci.writeU16BE(0);
    m2ci.writeU16BE(0);
    m2ci.writeU16BE(0);
    for (int i = 0; i < 4; ++i) m2ci.writeU32BE(0);

    // M2TD (texel data) chunk: NumLOD(2) + reserved(2) + offsets[NumLOD](4
    // each) + raw LOD bytes back-to-back. One LOD, 2x2 = 4 index bytes.
    ByteWriter m2td;
    m2td.writeU16BE(1);
    m2td.writeU16BE(0);
    m2td.writeU32BE(4 + 4 * 1); // LODDataOffset[0], matches Texel_ComputeSizeOffsets()
    uint8_t lod0[4] = {0, 1, 0, 1}; // red, green, red, green
    m2td.writeBytes(lod0, 4);

    IffFormWriter form(ID_TXTR);
    form.addChunk(ID_M2TX, m2tx.bytes());
    form.addChunk(ID_M2PI, m2pi.bytes());
    form.addChunk(ID_M2CI, m2ci.bytes());
    form.addChunk(ID_M2TD, m2td.bytes());
    return form.finish();
}
} // namespace

static void testParseSyntheticTexture() {
    std::vector<uint8_t> bytes = buildSynthetic2x2Utf();
    Texture tex = Texture::load(bytes.data(), bytes.size());

    CHECK(tex.header().minXSize == 2);
    CHECK(tex.header().minYSize == 2);
    CHECK(tex.header().numLOD == 1);
    CHECK(tex.hasPip());
    CHECK(!tex.isCompressed());
    CHECK(tex.pip().colors.size() == 2);
    CHECK(tex.hasDci());
    CHECK(tex.lods().size() == 1);
    CHECK(tex.lods()[0].data.size() == 4);
    CHECK(tex.lodWidth(0) == 2 && tex.lodHeight(0) == 2);

    auto rgba = tex.decodeLodToRgba(0);
    CHECK(rgba.size() == 4);
    CHECK(rgba[0].r == 255 && rgba[0].g == 0 && rgba[0].b == 0); // index 0 -> red
    CHECK(rgba[1].r == 0 && rgba[1].g == 255 && rgba[1].b == 0); // index 1 -> green
    CHECK(rgba[2].r == 255 && rgba[2].g == 0 && rgba[2].b == 0);
    CHECK(rgba[3].r == 0 && rgba[3].g == 255 && rgba[3].b == 0);
}

// Real game fixtures (3DOM2VIZ): close.utf is a single FORM TXTR with
// 4-bit PIP indices; roofs.utf is a 'CAT ' container holding multiple
// FORM TXTRs. Paths come from CMake args.
static void testRealSingleFormTexture(const char* path) {
    auto textures = Texture::loadAllFromFile(path);
    CHECK(textures.size() == 1);
    const Texture& tex = textures[0];
    // Verified by hex dump: 48x20, TexFormat 0x0404 (color, 4-bit index).
    CHECK(tex.header().minXSize == 48);
    CHECK(tex.header().minYSize == 20);
    CHECK(tex.header().texFormat == 0x0404);
    CHECK(tex.hasPip());
    CHECK(tex.pip().colors.size() == 16);
    auto rgba = tex.decodeLodToRgba(0);
    CHECK(rgba.size() == 48u * 20u);
}

static void testRealCatContainer(const char* path, const char* ppmOut) {
    auto textures = Texture::loadAllFromFile(path);
    CHECK(!textures.empty()); // CAT containers may hold one or many textures
    for (const auto& tex : textures) {
        auto rgba = tex.decodeLodToRgba(0);
        CHECK(rgba.size() == size_t(tex.lodWidth(0)) * tex.lodHeight(0));
    }
    std::printf("libm2texture_tests: %s holds %zu textures\n", path, textures.size());

    if (ppmOut) {
        const Texture& tex = textures[0];
        auto rgba = tex.decodeLodToRgba(0);
        FILE* f = std::fopen(ppmOut, "wb");
        CHECK(f != nullptr);
        std::fprintf(f, "P6\n%u %u\n255\n", tex.lodWidth(0), tex.lodHeight(0));
        for (const auto& px : rgba) {
            unsigned char rgb[3] = {px.r, px.g, px.b};
            std::fwrite(rgb, 1, 3, f);
        }
        std::fclose(f);
        std::printf("libm2texture_tests: wrote PPM dump to %s\n", ppmOut);
    }
}

// Encodes an image with the UTF encoder, decodes it back with the normal
// reader, and requires the pixels to survive the trip — the real proof the
// encoder emits a file our own (reference-derived) parser accepts. Covers
// both the opaque and alpha paths.
void testEncoderRoundTrip() {
    constexpr uint32_t kW = 37, kH = 11; // deliberately not power-of-two
    std::vector<Rgba8> src(size_t(kW) * kH);
    for (uint32_t y = 0; y < kH; ++y) {
        for (uint32_t x = 0; x < kW; ++x) {
            Rgba8& p = src[size_t(y) * kW + x];
            p.r = uint8_t(x * 7);
            p.g = uint8_t(y * 23);
            p.b = uint8_t((x + y) * 5);
            p.a = 255;
        }
    }

    // --- opaque (no alpha plane stored) ---
    {
        EncodeOptions o = defaultEncodeOptions(src.data(), kW, kH);
        CHECK(o.alphaDepth == 0); // fully opaque source => no alpha plane
        std::vector<uint8_t> utf = encodeUtf(src.data(), kW, kH, o);
        Texture t = Texture::load(utf.data(), utf.size());
        CHECK(t.header().numLOD == 1);
        CHECK(t.lodWidth(0) == kW);
        CHECK(t.lodHeight(0) == kH);
        std::vector<Rgba8> back = t.decodeLodToRgba(0);
        CHECK(back.size() == src.size());
        for (size_t i = 0; i < src.size(); ++i) {
            CHECK(back[i].r == src[i].r);
            CHECK(back[i].g == src[i].g);
            CHECK(back[i].b == src[i].b);
        }
    }

    // --- with alpha ---
    {
        std::vector<Rgba8> withA = src;
        for (size_t i = 0; i < withA.size(); ++i) {
            withA[i].a = uint8_t((i % 2) ? 0 : 255);
        }
        EncodeOptions o = defaultEncodeOptions(withA.data(), kW, kH);
        CHECK(o.alphaDepth == 7); // transparency detected
        std::vector<uint8_t> utf = encodeUtf(withA.data(), kW, kH, o);
        Texture t = Texture::load(utf.data(), utf.size());
        std::vector<Rgba8> back = t.decodeLodToRgba(0);
        CHECK(back.size() == withA.size());
        for (size_t i = 0; i < withA.size(); ++i) {
            CHECK(back[i].r == withA[i].r);
            CHECK(back[i].g == withA[i].g);
            CHECK(back[i].b == withA[i].b);
            // 7-bit alpha: fully opaque/transparent must survive exactly.
            CHECK((back[i].a > 200) == (withA[i].a == 255));
        }
    }
    std::printf("libm2texture_tests: encoder round-trip (opaque + alpha) OK\n");
}

int main(int argc, char** argv) {
    try {
        testParseSyntheticTexture();
        testEncoderRoundTrip();
        if (argc > 1) testRealSingleFormTexture(argv[1]);
        if (argc > 2) testRealCatContainer(argv[2], argc > 3 ? argv[3] : nullptr);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "UNCAUGHT EXCEPTION: %s\n", e.what());
        return 1;
    }
    std::printf("libm2texture_tests: all checks passed\n");
    return 0;
}
