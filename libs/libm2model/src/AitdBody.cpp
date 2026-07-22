#include "m2model/AitdPak.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace m2model {

namespace {
// Little-endian readers with bounds checking via a cursor.
struct Cursor {
    const uint8_t* p;
    size_t n;
    size_t pos = 0;
    bool ok = true;
    uint8_t u8() {
        if (pos + 1 > n) { ok = false; return 0; }
        return p[pos++];
    }
    int16_t s16() {
        if (pos + 2 > n) { ok = false; return 0; }
        int16_t v = int16_t(uint16_t(p[pos]) | (uint16_t(p[pos + 1]) << 8));
        pos += 2;
        return v;
    }
    uint16_t u16() {
        if (pos + 2 > n) { ok = false; return 0; }
        uint16_t v = uint16_t(uint16_t(p[pos]) | (uint16_t(p[pos + 1]) << 8));
        pos += 2;
        return v;
    }
    void skip(size_t k) { pos = std::min(n, pos + k); }
};

constexpr uint16_t INFO_ANIM = 2;
constexpr uint16_t INFO_TORTUE = 4;
constexpr uint16_t INFO_OPTIMISE = 8;
} // namespace

AitdBody parseAitdBody(const std::vector<uint8_t>& data) {
    // Layout (fitd hqr.cpp createBodyFromPtr):
    //   u16 flags
    //   s16 zv[6]                       bounding box
    //   u16 scratchSize; u8 scratch[scratchSize]
    //   u16 numVertices; vertex[numVertices] = 3 x s16
    //   if flags & INFO_ANIM: group table (size depends on INFO_OPTIMISE)
    //   u16 numPrimitives; primitives...
    AitdBody b;
    Cursor c{data.data(), data.size()};
    uint16_t flags = c.u16();
    for (int i = 0; i < 6; ++i) {
        b.bbox[i] = c.s16();
    }
    uint16_t scratchSize = c.u16();
    c.skip(scratchSize);

    uint16_t numVertices = c.u16();
    if (!c.ok || numVertices == 0 || numVertices > 20000) {
        return b;
    }
    b.vertices.resize(size_t(numVertices) * 3);
    for (uint16_t i = 0; i < numVertices; ++i) {
        b.vertices[size_t(i) * 3 + 0] = c.s16();
        b.vertices[size_t(i) * 3 + 1] = c.s16();
        b.vertices[size_t(i) * 3 + 2] = c.s16();
    }
    if (!c.ok) {
        b.vertices.clear();
        return b;
    }

    if (flags & INFO_TORTUE) {
        b.valid = true; // geometry is enough; unusual sub-format
        return b;
    }

    if (flags & INFO_ANIM) {
        uint16_t numGroups = c.u16();
        // group-order table: one u16 per group
        for (uint16_t i = 0; i < numGroups; ++i) {
            c.u16();
        }
        // group records: 0x18 bytes (AITD2+, INFO_OPTIMISE) or 0x10 (AITD1)
        size_t recSize = (flags & INFO_OPTIMISE) ? 0x18 : 0x10;
        for (uint16_t i = 0; i < numGroups; ++i) {
            c.skip(recSize);
        }
        if (!c.ok) {
            b.valid = true; // keep vertices even if groups are truncated
            return b;
        }
    }

    uint16_t numPrimitives = c.u16();
    if (c.ok && numPrimitives <= 20000) {
        b.primitives.reserve(numPrimitives);
        for (uint16_t i = 0; i < numPrimitives && c.ok; ++i) {
            AitdPrimitive pr;
            pr.type = c.u8();
            switch (pr.type) {
                case AitdPrimitive::Line:
                case AitdPrimitive::Point:
                case AitdPrimitive::BigPoint:
                case AitdPrimitive::Zixel: {
                    pr.subType = c.u8();
                    pr.color = c.u8();
                    c.u8(); // even
                    size_t np = (pr.type == AitdPrimitive::Line) ? 2 : 1;
                    pr.points.resize(np);
                    for (size_t j = 0; j < np; ++j) pr.points[j] = c.u16() / 6;
                    break;
                }
                case AitdPrimitive::Sphere: {
                    pr.subType = c.u8();
                    pr.color = c.u8();
                    c.u8(); // even
                    pr.size = c.u16();
                    pr.points.resize(1);
                    pr.points[0] = c.u16() / 6;
                    break;
                }
                case AitdPrimitive::Poly:
                case AitdPrimitive::PolyTex8:
                case AitdPrimitive::PolyTex9:
                case AitdPrimitive::PolyTex10: {
                    uint8_t np = c.u8();
                    pr.subType = c.u8();
                    pr.color = c.u8();
                    pr.points.resize(np);
                    for (uint8_t j = 0; j < np; ++j) pr.points[j] = c.u16() / 6;
                    // Types 9 and 10 append a (u,v) byte pair per point.
                    // Type 8 does not — see fitd hqr.cpp createBodyFromPtr.
                    if (pr.type == AitdPrimitive::PolyTex9 ||
                        pr.type == AitdPrimitive::PolyTex10) {
                        pr.uvs.resize(size_t(np) * 2);
                        for (size_t j = 0; j < pr.uvs.size(); ++j) pr.uvs[j] = c.u8();
                    }
                    break;
                }
                default:
                    // Unknown primitive type — stop rather than misparse.
                    c.ok = false;
                    break;
            }
            if (c.ok) {
                b.primitives.push_back(std::move(pr));
            }
        }
    }

    b.valid = true;
    return b;
}

} // namespace m2model
