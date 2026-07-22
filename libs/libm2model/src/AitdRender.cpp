// Software renderer and OBJ exporter for Alone in the Dark 3D bodies.
#include "m2model/AitdPak.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace m2model {

namespace {
// Screen-space vertex after projection (z kept for depth sorting).
struct Proj {
    double x, y, z;
};
} // namespace

bool renderAitdBody(const AitdBody& body, uint8_t* rgbaOut, uint32_t width, uint32_t height,
                     const AitdCamera& cam, AitdRenderMode mode) {
    for (size_t i = 0; i < size_t(width) * height; ++i) {
        rgbaOut[i * 4 + 0] = 0x10;
        rgbaOut[i * 4 + 1] = 0x12;
        rgbaOut[i * 4 + 2] = 0x18;
        rgbaOut[i * 4 + 3] = 0xFF;
    }
    size_t n = body.vertexCount();
    if (!body.valid || n == 0 || width < 8 || height < 8) {
        return false;
    }

    // Fit from the geometry that is actually DRAWN. Two traps here:
    //  * the declared bounding box is often much larger than the mesh (it
    //    doubles as a collision volume), which shoved models into a corner
    //    at tiny scale;
    //  * the vertex array can hold vertices no primitive references (bone
    //    roots and anim helpers, typically sitting at the origin), which
    //    drag the midpoint off the visible mesh so models sat low in the
    //    frame and clipped off the bottom edge.
    std::vector<char> used(n, 0);
    size_t usedCount = 0;
    for (const auto& pr : body.primitives) {
        for (uint16_t idx : pr.points) {
            if (idx < n && !used[idx]) { used[idx] = 1; ++usedCount; }
        }
    }
    if (usedCount == 0) { // point clouds with no primitives: use everything
        std::fill(used.begin(), used.end(), 1);
    }

    double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
    for (size_t v = 0; v < n; ++v) {
        if (!used[v]) continue;
        for (int a = 0; a < 3; ++a) {
            double c = body.vertices[v * 3 + a];
            lo[a] = std::min(lo[a], c);
            hi[a] = std::max(hi[a], c);
        }
    }
    double cx = (lo[0] + hi[0]) * 0.5;
    double cy = (lo[1] + hi[1]) * 0.5;
    double cz = (lo[2] + hi[2]) * 0.5;
    double ext = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], 1.0});

    // Scale off the bounding SPHERE, not the per-axis extents: the sphere
    // radius is rotation-invariant, so the model keeps a constant size and
    // never clips as the user orbits.
    double radius = 1.0;
    for (size_t v = 0; v < n; ++v) {
        if (!used[v]) continue;
        double dx = body.vertices[v * 3 + 0] - cx;
        double dy = body.vertices[v * 3 + 1] - cy;
        double dz = body.vertices[v * 3 + 2] - cz;
        radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
    }

    double zoom = (cam.zoom > 0.01) ? cam.zoom : 1.0;
    double scale = 0.43 * std::min(width, height) / radius * zoom;
    // AITD is Y-down (negative Y is up), which matches screen Y after the
    // centring above; flipVertical is offered for oddly authored models.
    double ySign = cam.flipVertical ? -1.0 : 1.0;
    double cosY = std::cos(cam.yaw), sinY = std::sin(cam.yaw);
    double cosP = std::cos(cam.pitch), sinP = std::sin(cam.pitch);
    double ox = width * 0.5 + cam.panX * width;
    double oy = height * 0.5 + cam.panY * height;

    std::vector<Proj> pv(n);
    for (size_t v = 0; v < n; ++v) {
        double x = body.vertices[v * 3 + 0] - cx;
        double y = (body.vertices[v * 3 + 1] - cy) * ySign;
        double z = body.vertices[v * 3 + 2] - cz;
        double x1 = x * cosY - z * sinY;   // yaw about vertical
        double z1 = x * sinY + z * cosY;
        double y2 = y * cosP - z1 * sinP;  // pitch about horizontal
        double z2 = y * sinP + z1 * cosP;
        pv[v].x = ox + x1 * scale;
        pv[v].y = oy + y2 * scale;
        pv[v].z = z2;
    }

    auto plot = [&](int px, int py, uint8_t r, uint8_t g, uint8_t bl) {
        if (px < 0 || py < 0 || px >= int(width) || py >= int(height)) return;
        size_t o = (size_t(py) * width + px) * 4;
        rgbaOut[o] = r; rgbaOut[o + 1] = g; rgbaOut[o + 2] = bl;
    };
    auto drawLine = [&](const Proj& a, const Proj& b, uint8_t r, uint8_t g, uint8_t bl) {
        int steps = int(std::max(std::fabs(b.x - a.x), std::fabs(b.y - a.y))) + 1;
        for (int s = 0; s <= steps; ++s) {
            double t = double(s) / steps;
            plot(int(a.x + t * (b.x - a.x)), int(a.y + t * (b.y - a.y)), r, g, bl);
        }
    };

    const uint8_t* pal = aitdPalette();
    bool havePolys = false;
    for (const auto& pr : body.primitives) {
        if (pr.points.size() >= 3) { havePolys = true; break; }
    }

    if ((mode == AitdRenderMode::SolidMaterials || mode == AitdRenderMode::SolidFlat) &&
        havePolys) {
        struct Face {
            const AitdPrimitive* pr;
            double depth;
        };
        std::vector<Face> faces;
        faces.reserve(body.primitives.size());
        for (const auto& pr : body.primitives) {
            if (pr.points.size() < 3) continue;
            double d = 0;
            bool ok = true;
            for (uint16_t idx : pr.points) {
                if (idx >= n) { ok = false; break; }
                d += pv[idx].z;
            }
            if (ok) faces.push_back({&pr, d / double(pr.points.size())});
        }
        // Painter ordering is only a coarse pass; a per-pixel depth buffer
        // below resolves interpenetrating faces, which pure back-to-front
        // sorting renders incorrectly (models looked "broken" where limbs
        // cross the torso).
        std::sort(faces.begin(), faces.end(),
                   [](const Face& a, const Face& b) { return a.depth < b.depth; });
        std::vector<double> zbuf(size_t(width) * height, -1e30);

        bool materials = (mode == AitdRenderMode::SolidMaterials);
        for (const auto& f : faces) {
            const auto& pts = f.pr->points;
            size_t m = pts.size();
            double area2 = 0;
            for (size_t i = 0; i < m; ++i) {
                size_t j = (i + 1) % m;
                area2 += pv[pts[i]].x * pv[pts[j]].y - pv[pts[j]].x * pv[pts[i]].y;
            }
            uint8_t r, g, bl;
            if (materials) {
                // Faithful mode: the palette colour, unmodified. AITD has no
                // lighting model — the artists baked shading into their
                // choice of palette index per face, which is why adjacent
                // faces already read as lit. An added light term double-
                // shades the model and, with a two-sided winding test,
                // dimmed most of it (Emily Hartwood came out near-black
                // against a reference render of the same body).
                r = pal[f.pr->color * 3 + 0];
                g = pal[f.pr->color * 3 + 1];
                bl = pal[f.pr->color * 3 + 2];
            } else {
                // Neutral mode exists to read silhouette and form, so here
                // shading is wanted: back faces darker, near faces brighter.
                double shade = (area2 < 0) ? 0.62 : 1.0;
                double dnorm = f.depth / (ext * 0.5 + 1.0);
                shade *= std::clamp(0.72 + 0.28 * dnorm, 0.45, 1.15);
                uint8_t c = uint8_t(std::clamp(210.0 * shade, 0.0, 255.0));
                r = c; g = c; bl = uint8_t(c * 0.95);
            }

            double ymin = 1e30, ymax = -1e30;
            for (uint16_t idx : pts) {
                ymin = std::min(ymin, pv[idx].y);
                ymax = std::max(ymax, pv[idx].y);
            }
            int y0 = std::max(0, int(std::floor(ymin)));
            int y1 = std::min(int(height) - 1, int(std::ceil(ymax)));
            for (int y = y0; y <= y1; ++y) {
                double yc = y + 0.5;
                // Collect span crossings together with their interpolated
                // depth so the z-buffer can be filled across each span.
                struct Cross { double x, z; };
                Cross xs[32];
                int cnt = 0;
                for (size_t i = 0; i < m && cnt < 32; ++i) {
                    size_t j = (i + 1) % m;
                    double ya = pv[pts[i]].y, yb = pv[pts[j]].y;
                    if ((ya <= yc && yb > yc) || (yb <= yc && ya > yc)) {
                        double t = (yc - ya) / (yb - ya);
                        xs[cnt].x = pv[pts[i]].x + t * (pv[pts[j]].x - pv[pts[i]].x);
                        xs[cnt].z = pv[pts[i]].z + t * (pv[pts[j]].z - pv[pts[i]].z);
                        ++cnt;
                    }
                }
                std::sort(xs, xs + cnt,
                           [](const Cross& a, const Cross& b) { return a.x < b.x; });
                for (int k = 0; k + 1 < cnt; k += 2) {
                    double xL = xs[k].x, xR = xs[k + 1].x;
                    double zL = xs[k].z, zR = xs[k + 1].z;
                    int xa = std::max(0, int(std::ceil(xL - 0.5)));
                    int xb = std::min(int(width) - 1, int(std::floor(xR - 0.5)));
                    double span = (xR - xL) > 1e-9 ? (xR - xL) : 1e-9;
                    for (int x = xa; x <= xb; ++x) {
                        double t = ((x + 0.5) - xL) / span;
                        double z = zL + t * (zR - zL);
                        size_t zi = size_t(y) * width + size_t(x);
                        if (z > zbuf[zi]) { // nearer camera wins
                            zbuf[zi] = z;
                            plot(x, y, r, g, bl);
                        }
                    }
                }
            }
        }
        return true;
    }

    if (mode == AitdRenderMode::Wireframe) {
        for (const auto& pr : body.primitives) {
            size_t m = pr.points.size();
            if (m < 2) continue;
            bool ok = true;
            for (uint16_t idx : pr.points) {
                if (idx >= n) { ok = false; break; }
            }
            if (!ok) continue;
            const uint8_t* c = pal + pr.color * 3;
            size_t edges = (m == 2) ? 1 : m;
            for (size_t i = 0; i < edges; ++i) {
                drawLine(pv[pr.points[i]], pv[pr.points[(i + 1) % m]], c[0], c[1], c[2]);
            }
        }
        return true;
    }

    // Point cloud.
    for (size_t v = 0; v < n; ++v) {
        double depth = std::clamp(0.5 + 0.5 * (pv[v].z / (ext * 0.5 + 1.0)), 0.25, 1.0);
        uint8_t col = uint8_t(120 + 135 * depth);
        int px = int(pv[v].x), py = int(pv[v].y);
        plot(px, py, col, uint8_t(col * 0.9), 90);
        plot(px + 1, py, col, uint8_t(col * 0.9), 90);
        plot(px, py + 1, col, uint8_t(col * 0.9), 90);
        plot(px + 1, py + 1, col, uint8_t(col * 0.9), 90);
    }
    return true;
}

std::string exportAitdBodyObj(const AitdBody& body, const std::string& mtlFileName,
                               std::string* mtlOut) {
    std::string obj;
    obj += "# Alone in the Dark model exported by M2Suite\n";
    if (!mtlFileName.empty()) {
        obj += "mtllib " + mtlFileName + "\n";
    }
    char buf[160];
    // AITD is Y-down; negate Y and Z so the model stands upright in the
    // usual Y-up tools (same convention as the fitd PLY exporter).
    for (size_t v = 0; v < body.vertexCount(); ++v) {
        std::snprintf(buf, sizeof(buf), "v %.4f %.4f %.4f\n",
                       body.vertices[v * 3 + 0] / 1000.0,
                       -body.vertices[v * 3 + 1] / 1000.0,
                       -body.vertices[v * 3 + 2] / 1000.0);
        obj += buf;
    }

    const uint8_t* pal = aitdPalette();
    std::vector<bool> used(256, false);
    int lastColor = -1;
    for (const auto& pr : body.primitives) {
        if (pr.points.size() < 3) continue;
        bool ok = true;
        for (uint16_t idx : pr.points) {
            if (idx >= body.vertexCount()) { ok = false; break; }
        }
        if (!ok) continue;
        if (int(pr.color) != lastColor) {
            std::snprintf(buf, sizeof(buf), "usemtl aitd_%u\n", unsigned(pr.color));
            obj += buf;
            lastColor = pr.color;
        }
        used[pr.color] = true;
        obj += "f";
        for (uint16_t idx : pr.points) {
            std::snprintf(buf, sizeof(buf), " %u", unsigned(idx) + 1); // OBJ is 1-based
            obj += buf;
        }
        obj += "\n";
    }

    if (mtlOut) {
        mtlOut->clear();
        *mtlOut += "# M2Suite AITD palette materials\n";
        for (int c = 0; c < 256; ++c) {
            if (!used[c]) continue;
            std::snprintf(buf, sizeof(buf), "newmtl aitd_%d\nKd %.3f %.3f %.3f\n", c,
                           pal[c * 3 + 0] / 255.0, pal[c * 3 + 1] / 255.0,
                           pal[c * 3 + 2] / 255.0);
            *mtlOut += buf;
        }
    }
    return obj;
}

} // namespace m2model
