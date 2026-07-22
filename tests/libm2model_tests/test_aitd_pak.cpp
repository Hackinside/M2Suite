// Opens an Alone in the Dark PAK, decompresses its entries and reports
// whether the output looks like a valid AITD "body" (3D model).
//   test_aitd_pak <LISTBODY.PAK> [entryIndex] [out.obj]
//
// AITD body layout (little-endian) as used for validation here:
//   s16 flags
//   s16 bbox[6]           (present when flags & 2)
//   s16 unknown/2 bytes
//   u16 vertexCount
//   s16 vertices[vertexCount][3]
//   ... bones/primitives follow
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

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

int16_t rd16(const std::vector<uint8_t>& d, size_t o) {
    return int16_t(uint16_t(d[o]) | (uint16_t(d[o + 1]) << 8));
}

struct BodyInfo {
    bool ok = false;
    int flags = 0;
    int vertexCount = 0;
    size_t vertexOffset = 0;
};

// The AITD body header stores a bounding box (offsets 2..13), which gives
// exact coordinate bounds. Rather than hard-code a version-specific vertex
// offset, we locate the vertex array by finding a (count, start) pair whose
// every vertex falls inside that box — robust across AITD1/2/3 and the 3DO
// build. This turns "did the decompression work?" into a strong yes/no.
BodyInfo parseBody(const std::vector<uint8_t>& d) {
    BodyInfo b;
    if (d.size() < 40) {
        return b;
    }
    b.flags = rd16(d, 0);
    int minX = rd16(d, 2), maxX = rd16(d, 4);
    int minY = rd16(d, 6), maxY = rd16(d, 8);
    int minZ = rd16(d, 10), maxZ = rd16(d, 12);
    if (minX > maxX || minY > maxY || minZ > maxZ) {
        return b; // not a bounding-box body
    }
    auto inBox = [&](size_t o) {
        int x = rd16(d, o), y = rd16(d, o + 2), z = rd16(d, o + 4);
        return x >= minX && x <= maxX && y >= minY && y <= maxY && z >= minZ && z <= maxZ;
    };

    // The vertex count is a u16 shortly after the box; the vertices follow.
    // Several offsets can yield an all-in-box run (e.g. a field of (0,0,0)
    // padding trivially fits), so score each valid candidate by how well
    // its vertices actually FILL the declared box — the true vertex array
    // reaches the box extremes, padding does not — and keep the best.
    long long bestScore = -1;
    for (size_t countOff = 14; countOff <= 34; countOff += 2) {
        if (countOff + 2 > d.size()) {
            break;
        }
        int count = uint16_t(rd16(d, countOff));
        size_t start = countOff + 2;
        if (count < 3 || count > 8000 || start + size_t(count) * 6 > d.size()) {
            continue;
        }
        int vMinX = 32767, vMaxX = -32768, vMinY = 32767, vMaxY = -32768;
        int vMinZ = 32767, vMaxZ = -32768;
        bool allIn = true;
        for (int v = 0; v < count && allIn; ++v) {
            size_t o = start + size_t(v) * 6;
            if (!inBox(o)) {
                allIn = false;
                break;
            }
            int x = rd16(d, o), y = rd16(d, o + 2), z = rd16(d, o + 4);
            vMinX = std::min(vMinX, x); vMaxX = std::max(vMaxX, x);
            vMinY = std::min(vMinY, y); vMaxY = std::max(vMaxY, y);
            vMinZ = std::min(vMinZ, z); vMaxZ = std::max(vMaxZ, z);
        }
        if (!allIn) {
            continue;
        }
        // Score = how much of each box axis the vertices span (0..1 each),
        // times the count, so the real filled array wins over tiny/padding.
        auto span = [](int lo, int hi, int blo, int bhi) -> double {
            int bw = bhi - blo;
            return bw > 0 ? double(hi - lo) / bw : 1.0;
        };
        double fill = span(vMinX, vMaxX, minX, maxX) + span(vMinY, vMaxY, minY, maxY) +
                      span(vMinZ, vMaxZ, minZ, maxZ);
        long long score = (long long)(fill * 1000) + count;
        if (score > bestScore) {
            bestScore = score;
            b.vertexCount = count;
            b.vertexOffset = start;
            b.ok = true;
        }
    }
    return b;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <PAK> [entryIndex] [out.obj]\n", argv[0]);
        return 2;
    }
    try {
        m2model::AitdPak pak = m2model::AitdPak::openFromFile(argv[1]);
        std::printf("entries: %zu\n", pak.entryCount());
        CHECK(pak.entryCount() > 0);

        // Survey: decompress everything and see how many parse as bodies.
        size_t decompressed = 0, validBodies = 0, totalVerts = 0;
        for (size_t i = 0; i < pak.entryCount(); ++i) {
            std::vector<uint8_t> body = pak.read(i);
            if (body.empty()) {
                continue;
            }
            ++decompressed;
            BodyInfo info = parseBody(body);
            if (info.ok) {
                ++validBodies;
                totalVerts += size_t(info.vertexCount);
            }
        }
        std::printf("decompressed: %zu/%zu, parsed as bodies: %zu (%zu vertices total)\n",
                     decompressed, pak.entryCount(), validBodies, totalVerts);

        size_t index = (argc > 2) ? size_t(std::atoi(argv[2])) : 1;
        if (index < pak.entryCount()) {
            std::vector<uint8_t> body = pak.read(index);
            // Raw dump for format analysis.
            std::printf("--- entry %zu raw, first 64 bytes ---\n", index);
            for (size_t k = 0; k < 64 && k < body.size(); ++k) {
                if (k % 16 == 0) std::printf("\n%04zu: ", k);
                std::printf("%02x ", body[k]);
            }
            std::printf("\n--- as s16 (offset: value) ---\n");
            for (size_t k = 0; k + 2 <= 40 && k + 2 <= body.size(); k += 2) {
                std::printf("  %2zu: %6d\n", k, int(rd16(body, k)));
            }
            BodyInfo info = parseBody(body);
            std::printf("entry %zu: %zu bytes, flags=0x%04x, vertices=%d, valid=%s\n", index,
                         body.size(), info.flags, info.vertexCount, info.ok ? "yes" : "no");
            std::printf("first 48 bytes:");
            for (size_t k = 0; k < 48 && k < body.size(); ++k) {
                if (k % 16 == 0) std::printf("\n  ");
                std::printf("%02x ", body[k]);
            }
            std::printf("\n");
            // Candidate vertex counts at each early u16 offset, to locate
            // the real header field.
            for (size_t o = 0; o + 2 <= 24 && o + 2 <= body.size(); o += 2) {
                uint16_t v = uint16_t(rd16(body, o));
                size_t need = o + 2 + size_t(v) * 6;
                std::printf("  u16@%zu = %5u -> vertex block %s\n", o, v,
                             (v > 0 && v < 20000 && need <= body.size()) ? "FITS" : "no");
            }

            if (argc > 3) {
                // Render via the library parser (the authoritative one) so
                // the GUI's exact output can be eyeballed.
                m2model::AitdBody lib = m2model::parseAitdBody(body);
                std::printf("lib parse: %zu vertices, valid=%s\n", lib.vertexCount(),
                             lib.valid ? "yes" : "no");
                std::vector<uint8_t> rgba(size_t(512) * 512 * 4);
                m2model::AitdCamera cam;
                cam.yaw = 0.6;
                m2model::renderAitdBody(lib, rgba.data(), 512, 512, cam,
                                         m2model::AitdRenderMode::SolidMaterials);
                std::printf("primitives: %zu\n", lib.primitives.size());
                // Also write an OBJ next to the image so geometry can be
                // checked in any 3D tool.
                std::string mtl;
                std::string objText = m2model::exportAitdBodyObj(lib, "model.mtl", &mtl);
                std::string objPath = std::string(argv[3]) + ".obj";
                if (FILE* of = std::fopen(objPath.c_str(), "w")) {
                    std::fwrite(objText.data(), 1, objText.size(), of);
                    std::fclose(of);
                    std::printf("wrote OBJ to %s\n", objPath.c_str());
                }
                FILE* f = std::fopen(argv[3], "wb");
                if (f) {
                    std::fprintf(f, "P6\n512 512\n255\n");
                    for (size_t p = 0; p < size_t(512) * 512; ++p) {
                        std::fwrite(&rgba[p * 4], 1, 3, f);
                    }
                    std::fclose(f);
                    std::printf("rendered %zu-vertex body to %s\n", lib.vertexCount(), argv[3]);
                }
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR: %s\n", e.what());
        return 1;
    }
    return 0;
}
