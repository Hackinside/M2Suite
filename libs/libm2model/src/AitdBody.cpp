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

// Converts group-local vertices into model space.
//
// An animated AITD body does NOT store its vertices in model space. Each
// group (bone) owns a run of vertices stored as offsets from the group's
// "base vertex" — the vertex it hangs off in the parent group. To get a
// usable mesh, every vertex in a group must have its base vertex position
// added to it.
//
// This is done in stored group order, in place, exactly as fitd's renderer
// does it (renderer.cpp, the loop over m_groups after the bone deltas):
// groups are laid out parents-first, so by the time a child is processed
// its base vertex has already been moved into model space and the offsets
// cascade correctly down the hierarchy. Doing it in a different order, or
// out of place against a pristine copy, silently breaks limbs further down
// the chain.
//
// Skipping this step entirely is what left every limb bunched around the
// origin, which read as a "broken model" (AITD1 LISTBOD2.PAK entry 12 —
// Emily Hartwood — rendered as a jumble of overlapping shards).
void resolveGroupHierarchy(AitdBody& b) {
    const size_t n = b.vertexCount();
    for (const AitdGroup& g : b.groups) {
        if (g.baseVertex >= n) {
            continue; // corrupt table entry: leave those vertices alone
        }
        const int32_t bx = b.vertices[size_t(g.baseVertex) * 3 + 0];
        const int32_t by = b.vertices[size_t(g.baseVertex) * 3 + 1];
        const int32_t bz = b.vertices[size_t(g.baseVertex) * 3 + 2];
        for (uint16_t k = 0; k < g.vertexCount; ++k) {
            const size_t v = size_t(g.start) + k;
            if (v >= n) {
                break;
            }
            // The engine works in 16-bit fixed point and relies on wrapping;
            // clamping instead would distort models that legitimately reach
            // the edge of the range.
            b.vertices[v * 3 + 0] = int16_t(int32_t(b.vertices[v * 3 + 0]) + bx);
            b.vertices[v * 3 + 1] = int16_t(int32_t(b.vertices[v * 3 + 1]) + by);
            b.vertices[v * 3 + 2] = int16_t(int32_t(b.vertices[v * 3 + 2]) + bz);
        }
    }
}
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
        // Group-order table: one u16 per group, a byte offset into the
        // records that follow. Only animation playback needs the ordering,
        // so it is read and discarded here.
        for (uint16_t i = 0; i < numGroups; ++i) {
            c.u16();
        }
        // Group records are 0x18 bytes on AITD2+ (INFO_OPTIMISE), 0x10 on
        // AITD1 — the AITD2 record adds a rotation delta and a pad word.
        b.groups.reserve(numGroups);
        for (uint16_t i = 0; i < numGroups && c.ok; ++i) {
            AitdGroup g;
            g.start = uint16_t(c.u16() / 6); // byte offsets, 6 bytes/vertex
            g.vertexCount = c.u16();
            g.baseVertex = uint16_t(c.u16() / 6);
            g.parentGroup = int8_t(c.u8());
            g.groupNumber = int8_t(c.u8());
            g.transformType = int16_t(c.u16());
            g.delta[0] = int16_t(c.u16());
            g.delta[1] = int16_t(c.u16());
            g.delta[2] = int16_t(c.u16());
            if (flags & INFO_OPTIMISE) {
                c.u16(); // rotateDelta[0]
                c.u16(); // rotateDelta[1]
                c.u16(); // rotateDelta[2]
                c.u16(); // padding
            }
            b.groups.push_back(g);
        }
        if (!c.ok) {
            b.groups.clear();
            b.valid = true; // keep vertices even if the group table is short
            return b;
        }
        resolveGroupHierarchy(b);
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
