// Self-contained AITD tests: they synthesise their own inputs, so they run
// in CI without any game data (test_aitd_pak.cpp covers real PAKs, but it
// needs a disc, so it stays a manual tool).
//
// The regressions pinned here:
//   * PolyTexture9/10 carry a (u,v) byte pair per point. Skipping them
//     desynchronises the primitive stream, so every following primitive
//     decodes as garbage — that produced long spikes on rendered models.
//   * The renderer must frame a model from the geometry that is actually
//     drawn, not from the declared bounding box (which doubles as a
//     collision volume and can be far larger) and not from unreferenced
//     vertices (bone roots sitting at the origin). Both bugs pushed models
//     off-centre or shrank them into a corner.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "m2model/AitdImage.h"
#include "m2model/AitdPak.h"

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n", #cond, __FILE__, \
                          __LINE__);                                            \
            std::exit(1);                                                       \
        }                                                                       \
    } while (0)

namespace {

void put16(std::vector<uint8_t>& d, uint16_t v) {
    d.push_back(uint8_t(v & 0xFF));
    d.push_back(uint8_t(v >> 8));
}

// Builds a body blob in the exact layout parseAitdBody expects: flags,
// bounding box, scratch buffer, vertices, primitives.
std::vector<uint8_t> makeBody(const std::vector<int16_t>& verts,
                               const std::vector<uint8_t>& primBytes, size_t primCount,
                               const int16_t bbox[6]) {
    std::vector<uint8_t> d;
    put16(d, 0); // flags: no INFO_ANIM, no INFO_TORTUE
    for (int i = 0; i < 6; ++i) {
        put16(d, uint16_t(bbox[i]));
    }
    put16(d, 0); // scratch buffer size
    put16(d, uint16_t(verts.size() / 3));
    for (int16_t v : verts) {
        put16(d, uint16_t(v));
    }
    put16(d, uint16_t(primCount));
    d.insert(d.end(), primBytes.begin(), primBytes.end());
    return d;
}

// A flat-shaded triangle primitive (type 1).
void addPoly(std::vector<uint8_t>& p, uint8_t color, std::vector<uint16_t> idx) {
    p.push_back(1);
    p.push_back(uint8_t(idx.size()));
    p.push_back(0); // subType
    p.push_back(color);
    for (uint16_t i : idx) {
        put16(p, uint16_t(i * 6)); // indices are stored as byte offsets
    }
}

// A textured triangle (type 9): same header, then a (u,v) pair per point.
void addPolyTex9(std::vector<uint8_t>& p, uint8_t color, std::vector<uint16_t> idx) {
    p.push_back(9);
    p.push_back(uint8_t(idx.size()));
    p.push_back(0);
    p.push_back(color);
    for (uint16_t i : idx) {
        put16(p, uint16_t(i * 6));
    }
    for (size_t k = 0; k < idx.size(); ++k) {
        p.push_back(uint8_t(10 + k)); // u
        p.push_back(uint8_t(20 + k)); // v
    }
}

void testPolyTextureUvsKeepStreamInSync() {
    std::vector<int16_t> verts{0, 0, 0, 100, 0, 0, 0, 100, 0, 100, 100, 0};
    std::vector<uint8_t> prims;
    addPolyTex9(prims, 7, {0, 1, 2});
    addPoly(prims, 42, {1, 2, 3}); // must still decode correctly after it
    int16_t bbox[6] = {0, 100, 0, 100, 0, 100};

    m2model::AitdBody b = m2model::parseAitdBody(makeBody(verts, prims, 2, bbox));
    CHECK(b.valid);
    CHECK(b.vertexCount() == 4);
    CHECK(b.primitives.size() == 2);

    const auto& tex = b.primitives[0];
    CHECK(tex.type == m2model::AitdPrimitive::PolyTex9);
    CHECK(tex.textured());
    CHECK(tex.color == 7);
    CHECK(tex.points.size() == 3);
    CHECK(tex.uvs.size() == 6);
    CHECK(tex.uvs[0] == 10 && tex.uvs[1] == 20);
    CHECK(tex.uvs[4] == 12 && tex.uvs[5] == 22);

    // The whole point: the second primitive is intact.
    const auto& flat = b.primitives[1];
    CHECK(flat.type == m2model::AitdPrimitive::Poly);
    CHECK(flat.color == 42);
    CHECK(flat.uvs.empty());
    CHECK(flat.points.size() == 3);
    CHECK(flat.points[0] == 1 && flat.points[1] == 2 && flat.points[2] == 3);
    std::printf("  PolyTexture UVs consumed, following primitive intact\n");
}

// Returns the centroid of every non-background pixel, plus the painted count.
void paintedCentroid(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, double& cx,
                      double& cy, size_t& count) {
    cx = cy = 0;
    count = 0;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t o = (size_t(y) * w + x) * 4;
            // Background is the fixed 0x10/0x12/0x18 clear colour.
            if (rgba[o] == 0x10 && rgba[o + 1] == 0x12 && rgba[o + 2] == 0x18) {
                continue;
            }
            cx += x;
            cy += y;
            ++count;
        }
    }
    if (count) {
        cx /= double(count);
        cy /= double(count);
    }
}

void testRenderCentresOnDrawnGeometry() {
    // A square centred on the origin, plus an unreferenced vertex far away
    // (this is what bone/anim helper vertices look like), and a declared
    // bounding box an order of magnitude larger than the mesh.
    std::vector<int16_t> verts{
        -100, -100, 0, 100, -100, 0, 100, 100, 0, -100, 100, 0,
        0,    3000, 0, // never referenced by a primitive
    };
    std::vector<uint8_t> prims;
    addPoly(prims, 60, {0, 1, 2});
    addPoly(prims, 60, {0, 2, 3});
    int16_t bbox[6] = {-8000, 8000, -8000, 8000, -8000, 8000};

    m2model::AitdBody b = m2model::parseAitdBody(makeBody(verts, prims, 2, bbox));
    CHECK(b.valid);
    CHECK(b.vertexCount() == 5);
    CHECK(b.primitives.size() == 2);

    const uint32_t W = 256, H = 256;
    std::vector<uint8_t> rgba(size_t(W) * H * 4);
    m2model::AitdCamera cam; // identity: face-on, no zoom or pan
    CHECK(m2model::renderAitdBody(b, rgba.data(), W, H, cam,
                                   m2model::AitdRenderMode::SolidMaterials));

    double cx = 0, cy = 0;
    size_t painted = 0;
    paintedCentroid(rgba, W, H, cx, cy, painted);
    std::printf("  painted %zu px, centroid (%.1f, %.1f) of %ux%u\n", painted, cx, cy, W, H);

    // Centred: the huge bbox and the stray vertex must not shift it.
    CHECK(std::abs(cx - W / 2.0) < 4.0);
    CHECK(std::abs(cy - H / 2.0) < 4.0);
    // And filling a healthy share of the frame rather than a corner speck.
    CHECK(painted > size_t(W) * H / 8);
}

void testRenderKeepsSizeWhileOrbiting() {
    // A cube: rotating it must not change how much of the frame it needs,
    // which is what the rotation-invariant bounding-sphere fit guarantees.
    std::vector<int16_t> verts{-100, -100, -100, 100,  -100, -100, 100,  100, -100,
                                -100, 100,  -100, -100, -100, 100,  100,  -100, 100,
                                100,  100,  100,  -100, 100,  100};
    std::vector<uint8_t> prims;
    addPoly(prims, 60, {0, 1, 2});
    addPoly(prims, 60, {0, 2, 3});
    addPoly(prims, 70, {4, 5, 6});
    addPoly(prims, 70, {4, 6, 7});
    int16_t bbox[6] = {-100, 100, -100, 100, -100, 100};
    m2model::AitdBody b = m2model::parseAitdBody(makeBody(verts, prims, 4, bbox));
    CHECK(b.valid);

    const uint32_t W = 256, H = 256;
    std::vector<uint8_t> rgba(size_t(W) * H * 4);
    for (int step = 0; step < 8; ++step) {
        m2model::AitdCamera cam;
        cam.yaw = step * 0.7;
        cam.pitch = (step % 3) * 0.4 - 0.4;
        CHECK(m2model::renderAitdBody(b, rgba.data(), W, H, cam,
                                       m2model::AitdRenderMode::SolidMaterials));
        double cx = 0, cy = 0;
        size_t painted = 0;
        paintedCentroid(rgba, W, H, cx, cy, painted);
        CHECK(painted > 0);
        // Never clipped: nothing may touch the frame border.
        for (uint32_t x = 0; x < W; ++x) {
            for (uint32_t y : {uint32_t(0), H - 1}) {
                size_t o = (size_t(y) * W + x) * 4;
                CHECK(rgba[o] == 0x10 && rgba[o + 1] == 0x12 && rgba[o + 2] == 0x18);
            }
        }
    }
    std::printf("  cube stays inside the frame across 8 orbit steps\n");
}

void testObjExport() {
    std::vector<int16_t> verts{0, 0, 0, 100, 0, 0, 0, 100, 0};
    std::vector<uint8_t> prims;
    addPoly(prims, 5, {0, 1, 2});
    int16_t bbox[6] = {0, 100, 0, 100, 0, 100};
    m2model::AitdBody b = m2model::parseAitdBody(makeBody(verts, prims, 1, bbox));
    CHECK(b.valid);

    std::string mtl;
    std::string obj = m2model::exportAitdBodyObj(b, "model.mtl", &mtl);
    CHECK(!obj.empty());
    CHECK(obj.find("mtllib model.mtl") != std::string::npos);
    CHECK(obj.find("usemtl aitd_5") != std::string::npos);
    CHECK(obj.find("\nf ") != std::string::npos);
    CHECK(mtl.find("newmtl aitd_5") != std::string::npos);
    std::printf("  OBJ export emits geometry, materials and an mtllib line\n");
}

void testAitdImage() {
    const uint32_t W = 320, H = 200;
    // One un-padded page: 4 + 768 + W*H.
    std::vector<uint8_t> page;
    put16(page, uint16_t(W));
    put16(page, uint16_t(H));
    for (int i = 0; i < 256; ++i) {
        page.push_back(uint8_t(i));       // R
        page.push_back(uint8_t(255 - i)); // G
        page.push_back(uint8_t(i / 2));   // B
    }
    for (uint32_t i = 0; i < W * H; ++i) {
        page.push_back(uint8_t(i % 256));
    }
    CHECK(page.size() == 4 + 768 + size_t(W) * H);
    CHECK(m2model::looksLikeAitdImage(page.data(), page.size()));

    auto imgs = m2model::parseAitdImages(page);
    CHECK(imgs.size() == 1);
    CHECK(imgs[0].width == W && imgs[0].height == H);
    CHECK(imgs[0].rgba.size() == size_t(W) * H * 4);
    // Pixel 0 uses palette index 0, pixel 5 index 5.
    CHECK(imgs[0].rgba[0] == 0 && imgs[0].rgba[1] == 255 && imgs[0].rgba[3] == 0xFF);
    CHECK(imgs[0].rgba[5 * 4 + 0] == 5 && imgs[0].rgba[5 * 4 + 1] == 250);

    // Two pages padded to 64 KiB each, as .pics stores them.
    std::vector<uint8_t> padded;
    for (int p = 0; p < 2; ++p) {
        padded.insert(padded.end(), page.begin(), page.end());
        padded.resize(size_t(p + 1) * 65536, 0);
    }
    auto multi = m2model::parseAitdImages(padded);
    CHECK(multi.size() == 2);
    CHECK(multi[1].width == W && multi[1].height == H);

    // Garbage must be rejected rather than decoded into noise.
    std::vector<uint8_t> junk(70000, 0xAB);
    CHECK(!m2model::looksLikeAitdImage(junk.data(), junk.size()));
    CHECK(m2model::parseAitdImages(junk).empty());
    std::printf("  backdrop pages decode (single + padded multi-page), junk rejected\n");
}

} // namespace

int main() {
    std::printf("test_aitd_synth:\n");
    testPolyTextureUvsKeepStreamInSync();
    testRenderCentresOnDrawnGeometry();
    testRenderKeepsSizeWhileOrbiting();
    testObjExport();
    testAitdImage();
    std::printf("all AITD synth tests passed\n");
    return 0;
}
