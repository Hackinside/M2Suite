#pragma once

#include <cstdint>
#include <vector>

#include "m2model/AitdPak.h"

namespace m2model {

// Alone in the Dark room / floor geometry, from the ETAGE*.PAK archives.
//
// A "room" is not a mesh — AITD's visuals are the pre-rendered backdrops
// (see AitdImage.h). What the engine stores per room is a set of
// axis-aligned boxes: colliders that the player walks on and bumps into,
// and triggers that fire scripts. Reconstructing a floor from these gives
// its true layout and scale, which is what a stage viewer wants.
//
// An ETAGE archive holds every room of one floor in its FIRST entry,
// behind a u32 offset table; the second entry is the camera data. (A
// later engine revision put one room per entry instead — AITD-roomviewer
// switches on the entry count. The 3DO builds of AITD 1 and 2 both use the
// table form, with exactly two entries per archive.)
//
//   entry 0:
//     u32 roomOffsets[]   — the first value doubles as the table size, so
//                           roomCount = roomOffsets[0] / 4; a zero or
//                           out-of-range offset ends the list early
//
// Room layout (little-endian), relative to that room's offset:
//   u16 colliderOffset   @ 0    also relative to the room's offset
//   u16 triggerOffset    @ 2
//   s16 position[3]      @ 4    room origin, in engine units
//   u16 cameraCount      @ 10
//   u16 cameraIds[n]     @ 12
//
//   at colliderOffset (and likewise at triggerOffset):
//     u16 count
//     count x 16 bytes:
//       s16 lowerX, upperX, lowerY, upperY, lowerZ, upperZ
//       s16 id       @ 12
//       s16 flags    @ 14
//
// Derived from tigrouind's AITD-roomviewer (RoomLoader.cs).
struct AitdBox {
    int16_t lower[3] = {};
    int16_t upper[3] = {};
    int16_t id = 0;
    uint16_t flags = 0;

    // Flag bits, as the room viewer interprets them.
    bool undergroundFloor() const { return flags & 0x02; }
    bool roomLink() const { return flags & 0x04; }
    bool interactive() const { return flags & 0x08; }
};

struct AitdRoom {
    bool valid = false;
    int32_t position[3] = {}; // room origin in engine units
    std::vector<AitdBox> colliders;
    std::vector<AitdBox> triggers;
    std::vector<uint16_t> cameraIds;
};

// Parses one room at `base` within the entry. `valid` is false when the
// offsets do not describe a coherent room, which is how non-room data is
// rejected.
AitdRoom parseAitdRoom(const std::vector<uint8_t>& data, size_t base);

// Parses every room in an ETAGE archive's first entry.
std::vector<AitdRoom> parseAitdRoomArchive(const std::vector<uint8_t>& entryZero);

// Turns a set of rooms into renderable geometry, so a whole floor can be
// shown in the same viewport as the models. Each box becomes a cuboid,
// coloured by role:
//   walkable floor  grey        room link     blue
//   underground     dark grey   interactive   deep blue
//   trigger         dark red
// Rooms are offset by their stored position, which is what assembles the
// separate entries into one floor plan.
AitdBody buildRoomBody(const std::vector<AitdRoom>& rooms, bool includeTriggers);

} // namespace m2model
