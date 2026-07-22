#include "m2model/AitdRoom.h"

#include <algorithm>

namespace m2model {

namespace {

int16_t rdS16(const std::vector<uint8_t>& d, size_t o) {
    return int16_t(uint16_t(d[o]) | (uint16_t(d[o + 1]) << 8));
}
uint16_t rdU16(const std::vector<uint8_t>& d, size_t o) {
    return uint16_t(uint16_t(d[o]) | (uint16_t(d[o + 1]) << 8));
}

// Reads a count-prefixed list of 16-byte boxes. Returns false if the list
// would run past the end of the entry, which is the main way a non-room
// blob gives itself away.
bool readBoxList(const std::vector<uint8_t>& d, size_t at, std::vector<AitdBox>& out) {
    if (at + 2 > d.size()) {
        return false;
    }
    uint16_t count = rdU16(d, at);
    if (count > 4096) {
        return false; // no real room comes close to this
    }
    size_t need = at + 2 + size_t(count) * 16;
    if (need > d.size()) {
        return false;
    }
    out.reserve(count);
    size_t i = at + 2;
    for (uint16_t k = 0; k < count; ++k, i += 16) {
        AitdBox b;
        b.lower[0] = rdS16(d, i + 0);
        b.upper[0] = rdS16(d, i + 2);
        b.lower[1] = rdS16(d, i + 4);
        b.upper[1] = rdS16(d, i + 6);
        b.lower[2] = rdS16(d, i + 8);
        b.upper[2] = rdS16(d, i + 10);
        b.id = rdS16(d, i + 12);
        b.flags = rdU16(d, i + 14);
        out.push_back(b);
    }
    return true;
}

// Room boxes have no colour of their own — these are ours, chosen by
// nearest match in the AITD palette so the whole pipeline (renderer, OBJ
// materials, export) keeps working on palette indices alone. Roles have to
// stay distinguishable at a glance, without a legend.
uint8_t colourForBox(const AitdBox& b, bool trigger) {
    if (trigger) {
        return 82; // red    (179, 39, 71)
    }
    if (b.interactive()) {
        return 8; // deep blue (75, 83, 99)
    }
    if (b.roomLink()) {
        return 99; // blue-grey (103,143,143)
    }
    if (b.undergroundFloor()) {
        return 185; // dark grey (91, 91, 79)
    }
    return 179; // walkable grey (159,159,143)
}

} // namespace

AitdRoom parseAitdRoom(const std::vector<uint8_t>& data, size_t base) {
    AitdRoom r;
    if (base + 12 > data.size()) {
        return r;
    }
    // The box offsets are relative to the room, not to the entry — the
    // rooms of one floor all live inside a single entry.
    uint16_t colliderOffset = rdU16(data, base + 0);
    uint16_t triggerOffset = rdU16(data, base + 2);
    if (colliderOffset < 12 || triggerOffset < 12 || base + colliderOffset >= data.size() ||
        base + triggerOffset >= data.size()) {
        return r;
    }
    r.position[0] = rdS16(data, base + 4);
    r.position[1] = rdS16(data, base + 6);
    r.position[2] = rdS16(data, base + 8);

    uint16_t cameraCount = rdU16(data, base + 10);
    if (cameraCount <= 64 && base + 12 + size_t(cameraCount) * 2 <= data.size()) {
        for (uint16_t i = 0; i < cameraCount; ++i) {
            r.cameraIds.push_back(rdU16(data, base + 12 + size_t(i) * 2));
        }
    }

    if (!readBoxList(data, base + colliderOffset, r.colliders) ||
        !readBoxList(data, base + triggerOffset, r.triggers)) {
        return r;
    }
    // A room with no boxes at all is indistinguishable from a
    // coincidentally-valid header, so require some geometry.
    r.valid = !r.colliders.empty() || !r.triggers.empty();
    return r;
}

std::vector<AitdRoom> parseAitdRoomArchive(const std::vector<uint8_t>& entryZero) {
    std::vector<AitdRoom> rooms;
    if (entryZero.size() < 4) {
        return rooms;
    }
    auto rdU32 = [&](size_t o) {
        return uint32_t(entryZero[o]) | (uint32_t(entryZero[o + 1]) << 8) |
               (uint32_t(entryZero[o + 2]) << 16) | (uint32_t(entryZero[o + 3]) << 24);
    };
    // offsets[0] doubles as the table size, exactly like the PAK's own
    // offset table.
    uint32_t first = rdU32(0);
    if (first < 4 || first % 4 != 0 || first > entryZero.size()) {
        return rooms;
    }
    const uint32_t maxRooms = first / 4;
    for (uint32_t i = 0; i < maxRooms; ++i) {
        if (size_t(i) * 4 + 4 > entryZero.size()) {
            break;
        }
        uint32_t off = rdU32(size_t(i) * 4);
        if (off == 0 || off >= entryZero.size()) {
            break; // the table is zero-terminated in practice
        }
        rooms.push_back(parseAitdRoom(entryZero, off));
    }
    return rooms;
}

AitdBody buildRoomBody(const std::vector<AitdRoom>& rooms, bool includeTriggers) {
    AitdBody body;

    auto addBox = [&](const AitdBox& b, const int32_t origin[3], bool trigger) {
        // Engine units are 10x the room-position units the header stores,
        // which is the scale AITD-roomviewer applies to line the two up.
        const int32_t ox = origin[0] * 10;
        const int32_t oy = origin[1] * 10;
        const int32_t oz = origin[2] * 10;

        const int32_t x0 = b.lower[0] + ox, x1 = b.upper[0] + ox;
        const int32_t y0 = b.lower[1] + oy, y1 = b.upper[1] + oy;
        const int32_t z0 = b.lower[2] + oz, z1 = b.upper[2] + oz;

        const auto base = uint16_t(body.vertexCount());
        const int32_t corners[8][3] = {
            {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
            {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1},
        };
        for (const auto& c : corners) {
            // Rooms span far more than a model does, so coordinates are
            // clamped into the s16 the body format uses rather than
            // wrapping, which would fold distant rooms back on top of near
            // ones.
            for (int a = 0; a < 3; ++a) {
                body.vertices.push_back(int16_t(std::clamp(c[a], -32768, 32767)));
            }
        }

        static const uint8_t faces[6][4] = {
            {0, 1, 2, 3}, // -Z
            {5, 4, 7, 6}, // +Z
            {4, 0, 3, 7}, // -X
            {1, 5, 6, 2}, // +X
            {4, 5, 1, 0}, // -Y
            {3, 2, 6, 7}, // +Y
        };
        const uint8_t colour = colourForBox(b, trigger);
        for (const auto& f : faces) {
            AitdPrimitive p;
            p.type = AitdPrimitive::Poly;
            p.color = colour;
            p.points = {uint16_t(base + f[0]), uint16_t(base + f[1]), uint16_t(base + f[2]),
                        uint16_t(base + f[3])};
            body.primitives.push_back(std::move(p));
        }
    };

    for (const AitdRoom& r : rooms) {
        if (!r.valid) {
            continue;
        }
        for (const AitdBox& b : r.colliders) {
            addBox(b, r.position, false);
        }
        if (includeTriggers) {
            for (const AitdBox& b : r.triggers) {
                addBox(b, r.position, true);
            }
        }
    }

    body.valid = !body.primitives.empty();
    if (body.valid) {
        for (int a = 0; a < 3; ++a) {
            int16_t lo = 32767, hi = -32768;
            for (size_t v = 0; v < body.vertexCount(); ++v) {
                lo = std::min(lo, body.vertices[v * 3 + a]);
                hi = std::max(hi, body.vertices[v * 3 + a]);
            }
            body.bbox[a * 2 + 0] = lo;
            body.bbox[a * 2 + 1] = hi;
        }
    }
    return body;
}

} // namespace m2model
