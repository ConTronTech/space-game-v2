#pragma once
// Pure asteroid rules: no GL, no SDL, no engine types. Unit-tested in package/tests/test_asteroids.cpp.
//   * seeded generation of belts (a band around the sun in the shared plane between two planet orbits) and clusters (shells around planets/moons)
//   * struct-of-arrays storage, nothing allocated per frame
//   * shared lumpy meshes (3 levels x 4 variants), culling, LOD, triangle budget, nearest-N, physics-pool hysteresis
// Reuses the noise and icosphere helpers of planet_mesh.h (pure header of world/star_system).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "world/star_system/planet_mesh.h"
#include "world/star_system/star_system_api.h"

namespace world {

constexpr int kAsteroidLevels = 3;      // mesh levels 0..2: 20, 80, 320 triangles
constexpr int kAsteroidVariants = 4;

// ---- the field (struct of arrays) ----
struct AsteroidField {
    std::vector<double> x, y, z;            // world position (static)
    std::vector<float> radius;
    std::vector<float> ax, ay, az;          // unit spin axis
    std::vector<float> spinRate;            // degrees per second
    std::vector<float> spinPhase;           // degrees at t = 0
    std::vector<uint8_t> variant;           // shared mesh variant 0..3
    std::vector<uint16_t> ore;              // index into the ore table
    int count() const { return (int)x.size(); }
    size_t bytes() const { return x.size() * (3 * 8 + 4 * 6 + 1 + 2); }
};

struct OreTable {                            // from data/ores.json; empty = everything is "rock"
    std::vector<std::string> ids;
    std::vector<float> weights;              // rarity
    std::vector<float> color;                // rgb per ore
};

// Weighted ore pick for a random number r in [0,1). Empty table -> 0.
inline int pickOre(const std::vector<float>& weights, float r) {
    float total = 0;
    for (float w : weights) total += std::max(0.0f, w);
    if (total <= 0.0f) return 0;
    float acc = 0, t = std::clamp(r, 0.0f, 0.999999f) * total;
    for (size_t i = 0; i < weights.size(); i++) { acc += std::max(0.0f, weights[i]); if (t < acc) return (int)i; }
    return (int)weights.size() - 1;
}

struct TargetBody { Vec3d pos; double radius = 0; bool moon = false; };   // a planet or moon a cluster can surround

struct GenParams {
    unsigned seed = 1234;
    int beltCount = 1, beltAsteroids = 1500, clusterCount = 4, clusterAsteroids = 60;
    double beltMaxWidth = 3000.0;            // the belt fills at most this much of the gap between two orbits
};

struct BeltInfo { double inner = 0, outer = 0, halfHeight = 0; };    // recorded for tests / docs

namespace detail {
struct ARng {
    uint32_t s;
    explicit ARng(uint32_t seed) : s(hash32(seed ^ 0xa511e9b3U)) {}
    float f() { s = hash32(s + 0x9e3779b9U); return (float)(s >> 8) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * f(); }
    int irange(int a, int b) { return a + (int)(f() * (float)(b - a + 1)) % (b - a + 1); }
};
inline void pushAsteroid(AsteroidField& F, ARng& rng, Vec3d p, float radius, const OreTable& ores) {
    float ax, ay, az, l;
    do { ax = rng.range(-1, 1); ay = rng.range(-1, 1); az = rng.range(-1, 1); l = std::sqrt(ax * ax + ay * ay + az * az); } while (l > 1.0f || l < 0.1f);
    F.x.push_back(p.x); F.y.push_back(p.y); F.z.push_back(p.z);
    F.radius.push_back(radius);
    F.ax.push_back(ax / l); F.ay.push_back(ay / l); F.az.push_back(az / l);
    F.spinRate.push_back(rng.range(-1.0f, 1.0f) * 20.0f / (1.0f + radius / 10.0f));
    F.spinPhase.push_back(rng.range(0.0f, 360.0f));
    F.variant.push_back((uint8_t)rng.irange(0, kAsteroidVariants - 1));
    F.ore.push_back((uint16_t)pickOre(ores.weights, rng.f()));
}
// old game's belt size distribution: half are small (1-5), a quarter medium, a few big
inline float beltRadius(ARng& rng) {
    int roll = rng.irange(0, 99);
    if (roll < 50) return rng.range(1.0f, 5.0f);
    if (roll < 75) return rng.range(5.0f, 15.0f);
    if (roll < 90) return rng.range(15.0f, 35.0f);
    if (roll < 97) return rng.range(35.0f, 60.0f);
    return rng.range(60.0f, 120.0f);
}
inline float clusterRadius(ARng& rng) { return rng.irange(0, 9) == 0 ? rng.range(4.0f, 10.0f) : rng.range(0.5f, 4.0f); }
} // namespace detail

// Belts and clusters. `planetOrbits`: orbit radii of the planets around the sun (any order). No sun/planets (fewer than 2 orbits) = no belts;
// no targets = no clusters. Positions are in the world frame, sun at `sun`, belt plane = the XZ plane through the sun.
inline AsteroidField generateField(const GenParams& gp, const Vec3d& sun, std::vector<double> planetOrbits, const std::vector<TargetBody>& targets,
                                   const OreTable& ores, std::vector<BeltInfo>* beltsOut = nullptr) {
    AsteroidField F;
    detail::ARng rng(gp.seed);
    std::sort(planetOrbits.begin(), planetOrbits.end());

    // belts: distinct gaps between consecutive orbits, chosen by seeded shuffle
    std::vector<int> gaps;
    for (int i = 0; i + 1 < (int)planetOrbits.size(); i++) if (planetOrbits[i + 1] - planetOrbits[i] > 4000.0) gaps.push_back(i);
    for (int i = (int)gaps.size() - 1; i > 0; i--) std::swap(gaps[i], gaps[rng.irange(0, i)]);
    int belts = std::min<int>(std::max(0, gp.beltCount), (int)gaps.size());
    for (int b = 0; b < belts; b++) {
        double lo = planetOrbits[gaps[b]], hi = planetOrbits[gaps[b] + 1], gap = hi - lo;
        double margin = std::max(1500.0, gap * 0.15);
        double inner = lo + margin, outer = hi - margin;
        double width = std::min(outer - inner, gp.beltMaxWidth);
        double mid = inner + (outer - inner) * rng.range(0.3f, 0.7f);       // the band sits somewhere in the middle of the gap
        inner = std::max(inner, mid - width * 0.5); outer = std::min(outer, mid + width * 0.5);
        double half = std::clamp(0.008 * mid, 150.0, 1000.0);
        if (beltsOut) beltsOut->push_back({inner, outer, half});
        uint32_t noiseSeed = mixSeed(gp.seed, 900 + b);
        for (int n = 0, tries = 0; n < gp.beltAsteroids && tries < gp.beltAsteroids * 30; tries++) {
            double ang = rng.range(0.0f, 6.2831853f);
            double r = inner + (outer - inner) * rng.f();
            float dens = fbm((float)std::cos(ang) * 2.0f, (float)std::sin(ang) * 2.0f, (float)(r / 15000.0), noiseSeed, 3);   // patchy, not uniform
            if (rng.f() > 0.25f + 0.75f * dens) continue;
            double h = half * (rng.f() + rng.f() - 1.0);                      // triangular: thickest in the middle
            detail::pushAsteroid(F, rng, {sun.x + r * std::cos(ang), sun.y + h, sun.z + r * std::sin(ang)}, detail::beltRadius(rng), ores);
            n++;
        }
    }

    // clusters: shells around some planets / moons (outside the moons' orbits for planets)
    std::vector<int> order;
    for (int i = 0; i < (int)targets.size(); i++) order.push_back(i);
    for (int i = (int)order.size() - 1; i > 0; i--) std::swap(order[i], order[rng.irange(0, i)]);
    int clusters = std::min<int>(std::max(0, gp.clusterCount), (int)order.size());
    for (int c = 0; c < clusters; c++) {
        const TargetBody& t = targets[order[c]];
        double inner = t.moon ? t.radius * 3.0 + 100.0 : t.radius * 4.5 + 500.0;
        double width = std::max(400.0, t.radius * 3.0);
        for (int n = 0; n < gp.clusterAsteroids; n++) {
            float dx, dy, dz, l;
            do { dx = rng.range(-1, 1); dy = rng.range(-1, 1); dz = rng.range(-1, 1); l = std::sqrt(dx * dx + dy * dy + dz * dz); } while (l > 1.0f || l < 0.1f);
            double r = inner + width * rng.f();
            detail::pushAsteroid(F, rng, {t.pos.x + dx / l * r, t.pos.y + dy / l * r * 0.5, t.pos.z + dz / l * r}, detail::clusterRadius(rng), ores);   // flattened shell
        }
    }
    return F;
}

// ---- the shared meshes ----
struct AsteroidMesh {
    std::vector<float> verts;              // interleaved position xyz + normal xyz (unit-ish size: scaled by the radius when drawn)
    std::vector<uint16_t> indices;
    int vertexCount() const { return (int)(verts.size() / 6); }
    int triangleCount() const { return (int)(indices.size() / 3); }
    size_t bytes() const { return verts.size() * sizeof(float) + indices.size() * sizeof(uint16_t); }
};
constexpr int kAsteroidStride = 6 * (int)sizeof(float);

inline int asteroidTriangles(int level) { return meshTriangleCount(std::clamp(level, 0, kAsteroidLevels - 1)); }

// Lumpy icosphere: a seeded stretch per axis plus two noise octaves, same shape at every level (the noise depends on the direction only).
inline AsteroidMesh buildAsteroidMesh(int level, uint32_t variantSeed) {
    using detail::V3;
    level = std::clamp(level, 0, kAsteroidLevels - 1);
    std::vector<V3> dir;
    AsteroidMesh m;
    detail::icosphere(level, dir, m.indices);
    detail::ARng rng(variantSeed);
    const float sx = rng.range(0.75f, 1.25f), sy = rng.range(0.7f, 1.2f), sz = rng.range(0.75f, 1.25f);
    std::vector<V3> pos(dir.size());
    for (size_t i = 0; i < dir.size(); i++) {
        const V3& d = dir[i];
        float lump = fbm(d.x * 1.6f + 5.0f, d.y * 1.6f + 5.0f, d.z * 1.6f + 5.0f, variantSeed, 3) - 0.5f;
        float bump = fbm(d.x * 4.5f, d.y * 4.5f, d.z * 4.5f, variantSeed + 13U, 2) - 0.5f;
        float r = 1.0f + lump * 0.7f + bump * 0.25f;                       // about 0.6 .. 1.4
        pos[i] = {d.x * r * sx, d.y * r * sy, d.z * r * sz};
    }
    std::vector<V3> nrm(pos.size(), V3{0, 0, 0});
    for (size_t i = 0; i < m.indices.size(); i += 3) {
        const V3 &a = pos[m.indices[i]], &b = pos[m.indices[i + 1]], &c = pos[m.indices[i + 2]];
        V3 e1{b.x - a.x, b.y - a.y, b.z - a.z}, e2{c.x - a.x, c.y - a.y, c.z - a.z};
        V3 f{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        for (int k = 0; k < 3; k++) { V3& t = nrm[m.indices[i + k]]; t.x += f.x; t.y += f.y; t.z += f.z; }
    }
    m.verts.resize(pos.size() * 6);
    for (size_t i = 0; i < pos.size(); i++) {
        V3 n = detail::normalized(nrm[i]);
        float* o = &m.verts[i * 6];
        o[0] = pos[i].x; o[1] = pos[i].y; o[2] = pos[i].z; o[3] = n.x; o[4] = n.y; o[5] = n.z;
    }
    return m;
}

// ---- drawing decisions ----
// Is a sphere of `radius` at distance `dist`, whose centre is `dotFwd` along the camera axis, inside a view cone of half angle acos(cosHalf)?
inline bool inViewCone(double dist, double dotFwd, double radius, double cosHalf) {
    if (dist <= radius) return true;
    double angle = std::acos(std::clamp(dotFwd / dist, -1.0, 1.0));
    double half = std::acos(std::clamp(cosHalf, -1.0, 1.0));
    return angle <= half + std::asin(std::min(1.0, radius / dist));
}

// Half angle (radians) of the cone around the view axis that contains the whole screen (its corner), for a vertical fov and aspect ratio.
inline double viewConeHalfAngle(double fovDegVertical, double aspect) {
    double t = std::tan(fovDegVertical * 3.14159265358979 / 360.0);
    return std::atan(t * std::sqrt(1.0 + aspect * aspect));
}

// Mesh level (0..2) for an asteroid of `px` pixels radius, or -1 for "draw as a point" (smaller than pointPx).
inline int asteroidLod(float px, float edgePx, float pointPx) {
    if (px < pointPx) return -1;
    return std::clamp((int)std::ceil(wantedLevel(px, edgePx)), 0, kAsteroidLevels - 1);
}

// `levels` is ordered nearest first. Lowers the farthest asteroids first until the triangle sum fits; if even level 0 does not fit, the farthest
// ones become points (-1). Returns the triangle count.
inline int applyAsteroidBudget(std::vector<int>& levels, int budget) {
    auto tris = [](int l) { return l < 0 ? 0 : asteroidTriangles(l); };
    int total = 0;
    for (int l : levels) total += tris(l);
    for (int i = (int)levels.size() - 1; i >= 0 && total > budget; i--) {
        while (levels[i] >= 0 && total > budget) { total -= tris(levels[i]); levels[i]--; total += tris(levels[i]); }
    }
    return total;
}

// The n indices nearest to p, nearest first.
inline void nearestIndices(const AsteroidField& F, const Vec3d& p, int n, std::vector<int>& out) {
    out.clear();
    if (n <= 0) return;
    std::vector<std::pair<double, int>> d;
    d.reserve(F.count());
    for (int i = 0; i < F.count(); i++) {
        double dx = F.x[i] - p.x, dy = F.y[i] - p.y, dz = F.z[i] - p.z;
        d.push_back({dx * dx + dy * dy + dz * dz, i});
    }
    n = std::min<int>(n, (int)d.size());
    std::partial_sort(d.begin(), d.begin() + n, d.end());
    for (int i = 0; i < n; i++) out.push_back(d[i].second);
}

// ---- the physics pool ----
// +1 = register a body now, -1 = remove it, 0 = leave it. Registers inside `radius`, removes outside radius * hysteresis (>= 1).
inline int poolAction(double dist, bool registered, double radius, double hysteresis) {
    if (!registered) return dist <= radius ? 1 : 0;
    return dist > radius * std::max(1.0, hysteresis) ? -1 : 0;
}

} // namespace world
