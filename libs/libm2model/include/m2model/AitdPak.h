#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace m2model {

// Alone in the Dark PAK archive reader.
//
// Layout (little-endian, as shipped on the 3DO disc):
//   u32 offsets[N]      — offsets[0] is 0; offsets[1] doubles as the table
//                         size, so N = offsets[1] / 4
//   per entry, at offsets[i]:
//     u32 additionalHeaderSize
//     u32 compressedSize
//     u32 uncompressedSize
//     u8  compressionType   (0 = stored, 1 = AITD LZSS, 4 = "deflate"-ish)
//     u8  info[3]
//     ... payload
//
// LISTBODY.PAK / LISTBOD2.PAK hold the 3D character models, LISTANIM /
// LISTANI2 the animations, ETAGE00-07 the rooms.
class AitdPak {
public:
    static AitdPak openFromFile(const std::filesystem::path& path);
    static AitdPak open(std::vector<uint8_t> bytes);

    size_t entryCount() const { return offsets_.size(); }
    // Returns the decompressed bytes of one entry (empty if the entry is
    // absent/unreadable).
    std::vector<uint8_t> read(size_t index) const;
    uint8_t compressionType(size_t index) const;

private:
    std::vector<uint8_t> data_;
    std::vector<uint32_t> offsets_;
};

// A parsed Alone in the Dark 3D body: vertices plus drawable primitives
// (polygons, lines, points, spheres), decoded per the fitd reference
// (hqr.cpp createBodyFromPtr). Colours index the AITD palette.
struct AitdPrimitive {
    enum Type { Line = 0, Poly = 1, Point = 2, Sphere = 3, Disk = 4, Cylinder = 5,
                BigPoint = 6, Zixel = 7, PolyTex8 = 8, PolyTex9 = 9, PolyTex10 = 10 };
    uint8_t type = 0;
    uint8_t color = 0;    // AITD palette index
    uint8_t subType = 0;  // poly fill style (flat / dithered / gouraud)
    uint16_t size = 0;    // sphere radius
    std::vector<uint16_t> points; // vertex indices
    // Texture coordinates for PolyTexture9/10, two bytes (u,v) per point.
    // AITD stores them in 0..255 texel space. Empty for untextured
    // primitives. Consuming these is not optional: they sit inline in the
    // primitive stream, and skipping them desynchronises every primitive
    // that follows (which is what produced the long spikes on some models).
    std::vector<uint8_t> uvs;
    bool textured() const { return type >= PolyTex8; }
};

struct AitdBody {
    bool valid = false;
    int16_t bbox[6] = {}; // ZVX1,ZVX2,ZVY1,ZVY2,ZVZ1,ZVZ2 (min/max per axis)
    std::vector<int16_t> vertices; // flat x,y,z triples
    std::vector<AitdPrimitive> primitives;
    size_t vertexCount() const { return vertices.size() / 3; }
};

// Parses a decompressed body blob following the exact fitd layout: flags,
// bounding box, a variable scratch buffer, the vertex list, an optional
// bone/group section, then the primitive list.
AitdBody parseAitdBody(const std::vector<uint8_t>& data);

enum class AitdRenderMode {
    SolidMaterials, // depth-sorted filled polygons, AITD palette colours
    SolidFlat,      // depth-sorted filled polygons, neutral shading only
    Wireframe,      // polygon edges only
    Points          // vertex cloud
};

// Free camera for the model viewport. The model is auto-fitted from its
// actual vertex extents (the declared bounding box is often much larger
// than the geometry, which pushed models into a corner), then adjusted by
// these controls.
struct AitdCamera {
    double yaw = 0.0;    // radians, spin about the vertical axis
    double pitch = 0.0;  // radians, tilt
    double zoom = 1.0;   // 1.0 = fit to viewport
    double panX = 0.0;   // fraction of viewport width
    double panY = 0.0;   // fraction of viewport height
    bool flipVertical = false; // for models authored the other way up
};

// Software-renders a body to an RGBA8 buffer (width*height*4). Returns
// false if the body has no geometry.
bool renderAitdBody(const AitdBody& body, uint8_t* rgbaOut, uint32_t width, uint32_t height,
                     const AitdCamera& camera, AitdRenderMode mode);

// Exports a body as Wavefront OBJ text (vertices + polygon faces, with
// per-colour materials referencing an accompanying .mtl). Returns the OBJ
// text; `mtlOut` receives the material library if non-null.
std::string exportAitdBodyObj(const AitdBody& body, const std::string& mtlFileName,
                               std::string* mtlOut);

// The 256-colour AITD palette (RGB triples). Returns a pointer to 768 bytes.
const uint8_t* aitdPalette();

} // namespace m2model
