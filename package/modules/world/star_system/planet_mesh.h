#pragma once
// Pure planet terrain meshes and level-of-detail rules: no GL, no SDL, no engine types. Unit-tested in package/tests/test_planet_mesh.cpp.
//   * icosphere of subdivision level 0..kMaxMeshLevel=5 (10*4^L + 2 shared vertices, 20*4^L triangles), unit radius
//   * seeded value-noise FBM displacement (a few percent of the radius) and biome colours from height + latitude
//   * per-vertex normals of the displaced mesh (smooth, no seams)
//   * LOD choice from the projected size with hysteresis, and a per-frame triangle budget
// Meshes are built for radius 1 and drawn scaled, so they do not depend on the body's size.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace world {

constexpr int kMaxMeshLevel = 5;   // 5 = 20,480 triangles (~6 ms to build); 6 (~26 ms) would hitch a frame on the one-build-per-frame path. docs/QUALITY.md

// ---- deterministic noise (own implementation: same numbers on every platform) ----
inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
inline uint32_t mixSeed(uint32_t a, uint32_t b) { return hash32(a * 0x9e3779b1U + hash32(b + 0x85ebca6bU)); }

inline float lattice(int x, int y, int z, uint32_t seed) {
    uint32_t h = hash32((uint32_t)x * 374761393U + (uint32_t)y * 668265263U + (uint32_t)z * 2147483647U + seed * 0x9e3779b1U);
    return (float)(h >> 8) * (1.0f / 16777215.0f);                 // [0,1]
}

inline float valueNoise(float x, float y, float z, uint32_t seed) {
    float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    int ix = (int)fx, iy = (int)fy, iz = (int)fz;
    float tx = x - fx, ty = y - fy, tz = z - fz;
    auto fade = [](float t) { return t * t * (3.0f - 2.0f * t); };
    tx = fade(tx); ty = fade(ty); tz = fade(tz);
    auto L = [&](int dx, int dy, int dz) { return lattice(ix + dx, iy + dy, iz + dz, seed); };
    float x00 = L(0, 0, 0) + (L(1, 0, 0) - L(0, 0, 0)) * tx, x10 = L(0, 1, 0) + (L(1, 1, 0) - L(0, 1, 0)) * tx;
    float x01 = L(0, 0, 1) + (L(1, 0, 1) - L(0, 0, 1)) * tx, x11 = L(0, 1, 1) + (L(1, 1, 1) - L(0, 1, 1)) * tx;
    float y0 = x00 + (x10 - x00) * ty, y1 = x01 + (x11 - x01) * ty;
    return y0 + (y1 - y0) * tz;                                    // [0,1]
}

inline float fbm(float x, float y, float z, uint32_t seed, int octaves) {
    float sum = 0, amp = 0.5f, norm = 0;
    for (int i = 0; i < octaves; i++) {
        sum += amp * valueNoise(x, y, z, seed + (uint32_t)i * 101U);
        norm += amp; amp *= 0.5f; x *= 2.0f; y *= 2.0f; z *= 2.0f;
    }
    return norm > 0 ? sum / norm : 0.0f;                           // [0,1]
}

// ---- planet description ----
struct PlanetParams {
    uint32_t seed = 1;             // per planet: mixSeed(world seed, body id)
    float terrainHeight = 0.03f;   // peak displacement as a fraction of the radius
    float color[3] = {0.6f, 0.55f, 0.4f};   // the body colour: the palette is derived from it
    bool moon = false;             // moons never have oceans
};

// Terrain height in [0,1] at a unit direction (before sea-level flattening).
inline float terrainNoise(float x, float y, float z, uint32_t seed) {
    float h = fbm(x * 2.2f, y * 2.2f, z * 2.2f, seed, 5) * 0.65f + fbm(x * 5.1f + 31.0f, y * 5.1f + 17.0f, z * 5.1f + 9.0f, seed + 7U, 3) * 0.35f;
    return std::clamp((h - 0.5f) * 2.0f + 0.5f, 0.0f, 1.0f);      // stretch the middle-heavy FBM over the full range
}

inline bool planetHasOcean(const PlanetParams& p) { return !p.moon && (hash32(p.seed ^ 0x5bd1e995U) % 3U) != 0U; }
constexpr float kSeaLevel = 0.42f;

// Radius of the drawn terrain at a unit direction, as a multiple of the body radius (oceans flattened at sea level): exactly what
// buildPlanetMesh puts at a vertex in that direction. Pure, so other modules (world/stations) can sit things on the ground.
inline float surfaceRadiusFactor(const PlanetParams& p, bool ocean, float x, float y, float z) {
    float h = terrainNoise(x, y, z, p.seed);
    float shown = ocean ? std::max(h, kSeaLevel) : h;             // oceans are flat
    return 1.0f + p.terrainHeight * (shown * 2.0f - 1.0f);
}
inline float surfaceRadiusFactor(const PlanetParams& p, float x, float y, float z) { return surfaceRadiusFactor(p, planetHasOcean(p), x, y, z); }

// ---- the mesh ----
struct PlanetMesh {
    std::vector<float> verts;              // interleaved: position xyz, normal xyz, colour rgb (9 floats = 36 bytes per vertex)
    std::vector<uint16_t> indices;         // triangles
    int level = 0;
    static constexpr int kStride = 9 * (int)sizeof(float);
    int vertexCount() const { return (int)(verts.size() / 9); }
    int triangleCount() const { return (int)(indices.size() / 3); }
    size_t bytes() const { return verts.size() * sizeof(float) + indices.size() * sizeof(uint16_t); }
};

inline int meshVertexCount(int level) { return 10 * (1 << (2 * level)) + 2; }
inline int meshTriangleCount(int level) { return 20 * (1 << (2 * level)); }

namespace detail {
struct V3 { float x, y, z; };
inline V3 normalized(V3 v) { float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); return l > 0 ? V3{v.x / l, v.y / l, v.z / l} : V3{0, 1, 0}; }
inline float smooth(float a, float b, float x) { float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
inline void mixc(const float* a, const float* b, float t, float* out) { for (int i = 0; i < 3; i++) out[i] = a[i] + (b[i] - a[i]) * t; }

// unit icosphere directions + triangles, subdivided `level` times with shared vertices
inline void icosphere(int level, std::vector<V3>& v, std::vector<uint16_t>& idx) {
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    v = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (auto& p : v) p = normalized(p);
    idx = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
           3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    for (int l = 0; l < level; l++) {
        std::unordered_map<uint32_t, uint16_t> mid;
        std::vector<uint16_t> out;
        out.reserve(idx.size() * 4);
        auto midpoint = [&](uint16_t a, uint16_t b) {
            uint32_t key = a < b ? ((uint32_t)a << 16) | b : ((uint32_t)b << 16) | a;
            auto it = mid.find(key);
            if (it != mid.end()) return it->second;
            V3 m = normalized({(v[a].x + v[b].x) * 0.5f, (v[a].y + v[b].y) * 0.5f, (v[a].z + v[b].z) * 0.5f});
            v.push_back(m);
            return mid[key] = (uint16_t)(v.size() - 1);
        };
        for (size_t i = 0; i < idx.size(); i += 3) {
            uint16_t a = idx[i], b = idx[i + 1], c = idx[i + 2];
            uint16_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
            out.insert(out.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
        }
        idx.swap(out);
    }
}
} // namespace detail

// Biome colour from height (0..1), latitude (|y| of the unit direction, 0 equator .. 1 pole) and the body colour. Blended, so vertices interpolate softly.
inline void biomeColor(const PlanetParams& p, bool ocean, float h, float lat, float* out) {
    using namespace detail;
    const float* c = p.color;
    const float water[3] = {0.08f, 0.22f, 0.6f}, sandT[3] = {0.8f, 0.72f, 0.5f}, greenT[3] = {0.2f, 0.5f, 0.2f}, grey[3] = {0.45f, 0.42f, 0.4f}, white[3] = {0.93f, 0.95f, 0.98f};
    float deep[3], sand[3], grass[3], rock[3];
    if (ocean) { mixc(c, water, 0.6f, deep); mixc(c, sandT, 0.6f, sand); mixc(c, greenT, 0.55f, grass); }
    else { for (int i = 0; i < 3; i++) { deep[i] = c[i] * 0.6f; sand[i] = c[i] * 0.88f; grass[i] = c[i] * 1.0f; } }   // barren: dark lowlands, body colour, lighter highlands
    mixc(c, grey, 0.6f, rock);
    float col[3];
    if (ocean && h < kSeaLevel) { mixc(deep, deep, 0, col); float t = smooth(0.0f, kSeaLevel, h); for (int i = 0; i < 3; i++) col[i] = deep[i] * (0.7f + 0.3f * t); }
    else {
        float lo = ocean ? kSeaLevel : 0.0f;
        float beach = smooth(lo, lo + 0.05f, h), high = smooth(0.55f, 0.70f, h);
        float base[3];
        mixc(sand, grass, smooth(lo + 0.03f, lo + 0.10f, h), base);
        mixc(base, rock, high, col);
        (void)beach;
    }
    float cold = lat * 0.75f + std::max(0.0f, h - 0.55f) * 1.2f;
    float snow = smooth(0.66f, 0.80f, cold);
    mixc(col, white, snow, out);
}

// Builds the terrain mesh of subdivision `level` (clamped to 0..kMaxMeshLevel).
inline PlanetMesh buildPlanetMesh(int level, const PlanetParams& p) {
    using namespace detail;
    level = std::clamp(level, 0, kMaxMeshLevel);
    std::vector<V3> dir;
    PlanetMesh m;
    m.level = level;
    icosphere(level, dir, m.indices);
    const bool ocean = planetHasOcean(p);
    const size_t n = dir.size();
    std::vector<V3> pos(n);
    std::vector<float> height(n);
    for (size_t i = 0; i < n; i++) {
        float h = terrainNoise(dir[i].x, dir[i].y, dir[i].z, p.seed);
        height[i] = h;
        float r = surfaceRadiusFactor(p, ocean, dir[i].x, dir[i].y, dir[i].z);
        pos[i] = {dir[i].x * r, dir[i].y * r, dir[i].z * r};
    }
    // normals: area-weighted average of the face normals around each vertex
    std::vector<V3> nrm(n, V3{0, 0, 0});
    for (size_t i = 0; i < m.indices.size(); i += 3) {
        const V3 &a = pos[m.indices[i]], &b = pos[m.indices[i + 1]], &c = pos[m.indices[i + 2]];
        V3 e1{b.x - a.x, b.y - a.y, b.z - a.z}, e2{c.x - a.x, c.y - a.y, c.z - a.z};
        V3 f{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        for (int k = 0; k < 3; k++) { V3& t = nrm[m.indices[i + k]]; t.x += f.x; t.y += f.y; t.z += f.z; }
    }
    m.verts.resize(n * 9);
    for (size_t i = 0; i < n; i++) {
        V3 nn = normalized(nrm[i]);
        float col[3];
        biomeColor(p, ocean, height[i], std::fabs(dir[i].y), col);
        float* o = &m.verts[i * 9];
        o[0] = pos[i].x; o[1] = pos[i].y; o[2] = pos[i].z;
        o[3] = nn.x; o[4] = nn.y; o[5] = nn.z;
        o[6] = col[0]; o[7] = col[1]; o[8] = col[2];
    }
    return m;
}

// ---- level of detail ----
// Approximate on-screen radius in pixels of a body of `radius` at `dist`, for a viewport `viewportH` pixels tall and vertical field of view `fovDeg`.
inline float pixelRadius(double radius, double dist, float viewportH, float fovDeg) {
    if (dist <= radius) return 1.0e6f;                             // at or inside the surface: as big as it gets
    double f = (double)viewportH * 0.5 / std::tan((double)fovDeg * 3.14159265358979 / 360.0);
    return (float)(radius / dist * f);
}

// Continuous level wanted for a body `px` pixels in radius: an icosphere edge spans about 1.1 * px / 2^level pixels, and we want it <= edgePx.
inline float wantedLevel(float px, float edgePx) {
    if (px <= 0.0f || edgePx <= 0.0f) return 0.0f;
    return std::log2(std::max(1e-3f, px * 1.107f / edgePx));
}

// The level to use now: `current` (-1 = none yet) is kept unless the wanted level moves clearly past it (hysteresis), so a body at a boundary does not flicker.
inline int chooseLod(float wanted, int current, int maxLevel, float hysteresis = 0.3f) {
    maxLevel = std::clamp(maxLevel, 0, kMaxMeshLevel);
    int target = std::clamp((int)std::ceil(wanted), 0, maxLevel);
    if (current < 0 || current > maxLevel) return target;
    if (target > current && wanted > (float)current + hysteresis) return target;              // clearly needs more detail
    if (target < current && wanted < (float)(current - 1) - hysteresis) return target;        // clearly needs less
    return current;
}

// Lowers levels (the least important, most expensive first) until the total triangle count fits the budget. Level 0 is never lowered further.
inline int applyTriangleBudget(std::vector<int>& levels, const std::vector<float>& pixelRadii, int budget) {
    auto total = [&] { int t = 0; for (int l : levels) t += meshTriangleCount(l); return t; };
    int t = total();
    while (t > budget) {
        int best = -1; float bestScore = -1;
        for (size_t i = 0; i < levels.size(); i++) {
            if (levels[i] <= 0) continue;
            float score = (float)meshTriangleCount(levels[i]) / std::max(1.0f, pixelRadii[i]);   // triangles spent per pixel of importance
            if (score > bestScore) { bestScore = score; best = (int)i; }
        }
        if (best < 0) break;
        t -= meshTriangleCount(levels[best]);
        levels[best]--;
        t += meshTriangleCount(levels[best]);
    }
    return t;
}

} // namespace world
