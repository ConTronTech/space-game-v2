#pragma once
// Pure asteroid rules: no GL, no SDL, no engine types. Unit-tested in package/tests/test_asteroids.cpp.
//   * seeded generation of belts (a band around the sun in the shared plane between two planet orbits; 1-5 per system, outer gaps first),
//     rings (a flat band around a planet, per-planet chance) and clusters (shells around ring-less planets and a few moons)
//   * struct-of-arrays storage, nothing allocated per frame
//   * shared lumpy meshes (3 levels x 4 variants), culling, LOD, triangle budget, nearest-N, physics-pool hysteresis
// Reuses the noise and icosphere helpers of planet_mesh.h (pure header of world/star_system).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
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

// ---- ore zones (data/ore_zones.json): distance bands from the sun that scale the base ore weights ----
struct OreZone {
    std::string id;
    double minDistance = 0, maxDistance = 0;                     // [min, max) from the sun, world units
    std::vector<std::pair<std::string, float>> multipliers;      // ore id -> weight multiplier (missing = 1)
};
struct OreZones { std::vector<OreZone> zones; };                 // ordered; empty = the single global table

// First zone whose [min, max) holds `distance`, else -1 (unaffected).
inline int findOreZone(const OreZones& z, double distance) {
    for (size_t i = 0; i < z.zones.size(); i++) if (distance >= z.zones[i].minDistance && distance < z.zones[i].maxDistance) return (int)i;
    return -1;
}
inline float oreZoneMultiplier(const OreZone& z, const std::string& ore) {
    for (auto& m : z.multipliers) if (m.first == ore) return m.second;
    return 1.0f;
}
// The ore table at `distance` from the sun: base weight x zone multiplier (clamped >= 0), renormalized to sum 1.
// No zone at that distance (or no zones at all) = the base table, unchanged.
inline OreTable zoneOreTable(const OreTable& base, const OreZones& zones, double distance, int* zoneOut = nullptr) {
    int zi = findOreZone(zones, distance);
    if (zoneOut) *zoneOut = zi;
    if (zi < 0) return base;
    OreTable t = base;
    float total = 0;
    for (size_t i = 0; i < t.weights.size(); i++) {
        float m = i < t.ids.size() ? oreZoneMultiplier(zones.zones[zi], t.ids[i]) : 1.0f;
        t.weights[i] = std::max(0.0f, base.weights[i]) * std::max(0.0f, m);
        total += t.weights[i];
    }
    if (total <= 0.0f) return base;                              // a zone that zeroes everything: keep the base table
    for (float& w : t.weights) w /= total;
    return t;
}

// One belt, ring or cluster of the generated field (for the --ore-zone-dump log and tests). `target` = index into the targets it surrounds (-1 = a belt).
struct OreGroup { bool belt = false; double distance = 0; int zone = -1; int first = 0, count = 0; bool ring = false; int target = -1; };

struct TargetBody { Vec3d pos; double radius = 0; bool moon = false; };   // a planet or moon a ring / cluster can surround

struct GenParams {
    unsigned seed = 1234;
    // belts: beltCount < 0 = seeded random in [beltCountMin, beltCountMax] per system; >= 0 forces that many (capped by the eligible gaps).
    // Gaps are picked weighted toward the outer ones (weight = (rank from the sun + 1)^2), so a 1-belt system usually has it far out.
    int beltCount = -1, beltCountMin = 1, beltCountMax = 5, beltAsteroids = 1500;
    double beltMaxWidth = 3000.0;            // the belt fills at most this much of the gap between two orbits
    // planets: each one rolls ringChance (seeded, per planet) for a flat ring; a planet without a ring gets a shell cluster instead (planetShells),
    // so every planet has something nearby. Moons: their own budget, moonClusterCount shell clusters over a seeded shuffle of the moons.
    float ringChance = 0.4f;
    int ringAsteroids = 300;
    bool planetShells = true;
    int moonClusterCount = 2, clusterAsteroids = 60;
};

struct BeltInfo { double inner = 0, outer = 0, halfHeight = 0; };    // recorded for tests / docs

// Ring band around a planet of radius R, in planet radii: inner edge 1.45-1.7 R, 0.3-0.5 R wide, never past kRingMaxOuter
// (moons orbit at >= 2.5 R + 150 minus at most 0.3 R of moon radius; the atmosphere shell is 1.4 R, user decision 2026-09-22:
// the ring's inner edge stays clear of it so the glow and the ring don't overlap).
constexpr double kRingMinInner = 1.45, kRingMaxInner = 1.7, kRingMaxOuter = 2.2;
inline double ringHalfHeight(double planetRadius) { return std::clamp(planetRadius * 0.02, 4.0, 25.0); }   // thin: a flat band, not a shell

// Belt count for a system: forced (>= 0) or a seeded roll in [lo, hi].
inline int rollBeltCount(const GenParams& gp) {
    if (gp.beltCount >= 0) return gp.beltCount;
    int lo = std::max(0, std::min(gp.beltCountMin, gp.beltCountMax)), hi = std::max(lo, gp.beltCountMax);
    uint32_t h = mixSeed(gp.seed, 950);
    return lo + (int)(h % (uint32_t)(hi - lo + 1));
}

// Does planet number `planetIndex` (0-based, in target order) have a ring? Seeded per planet, independent of every other roll.
inline bool planetHasRing(unsigned seed, int planetIndex, float chance) {
    if (chance <= 0.0f) return false;
    if (chance >= 1.0f) return true;
    float r = (float)(mixSeed(seed, 1000 + (uint32_t)planetIndex) >> 8) * (1.0f / 16777216.0f);
    return r < chance;
}

// Outward-biased pick of `n` distinct gaps from `gaps` (ordered inner -> outer): weighted sampling without replacement, weight (rank + 1)^2.
// Returns the picked gaps in pick order. Pure, uses only `r` (numbers in [0,1)).
template <class Rand>
inline std::vector<int> pickGapsOutward(const std::vector<int>& gaps, int n, Rand&& r) {
    std::vector<int> out;
    std::vector<double> w(gaps.size());
    for (size_t i = 0; i < gaps.size(); i++) w[i] = (double)(i + 1) * (double)(i + 1);
    n = std::clamp(n, 0, (int)gaps.size());
    for (int k = 0; k < n; k++) {
        double total = 0;
        for (double x : w) total += x;
        double t = std::clamp((double)r(), 0.0, 0.999999) * total, acc = 0;
        size_t pick = gaps.size();
        for (size_t i = 0; i < w.size(); i++) { if (w[i] <= 0) continue; acc += w[i]; pick = i; if (t < acc) break; }
        out.push_back(gaps[pick]);
        w[pick] = 0;
    }
    return out;
}

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

// Belts, rings and clusters. `planetOrbits`: orbit radii of the planets around the sun (any order). No sun/planets (fewer than 2 orbits) = no belts;
// no targets = no rings / clusters. Output order: belts, then per planet (target order) its ring or shell, then the moon shells. Positions are in the world frame, sun at `sun`, belt plane = the XZ plane through the sun.
inline AsteroidField generateField(const GenParams& gp, const Vec3d& sun, std::vector<double> planetOrbits, const std::vector<TargetBody>& targets,
                                   const OreTable& ores, std::vector<BeltInfo>* beltsOut = nullptr,
                                   const OreZones* zones = nullptr, std::vector<OreGroup>* groupsOut = nullptr) {
    static const OreZones kNoZones;
    const OreZones& oz = zones ? *zones : kNoZones;
    AsteroidField F;
    detail::ARng rng(gp.seed);
    std::sort(planetOrbits.begin(), planetOrbits.end());

    // belts: distinct gaps between consecutive orbits (inner -> outer), count rolled per system, picked with an outward bias
    std::vector<int> gaps;
    for (int i = 0; i + 1 < (int)planetOrbits.size(); i++) if (planetOrbits[i + 1] - planetOrbits[i] > 4000.0) gaps.push_back(i);
    gaps = pickGapsOutward(gaps, rollBeltCount(gp), [&rng] { return rng.f(); });
    int belts = (int)gaps.size();
    for (int b = 0; b < belts; b++) {
        double lo = planetOrbits[gaps[b]], hi = planetOrbits[gaps[b] + 1], gap = hi - lo;
        double margin = std::max(1500.0, gap * 0.15);
        double inner = lo + margin, outer = hi - margin;
        double width = std::min(outer - inner, gp.beltMaxWidth);
        double mid = inner + (outer - inner) * rng.range(0.3f, 0.7f);       // the band sits somewhere in the middle of the gap
        inner = std::max(inner, mid - width * 0.5); outer = std::min(outer, mid + width * 0.5);
        double half = std::clamp(0.008 * mid, 150.0, 1000.0);
        if (beltsOut) beltsOut->push_back({inner, outer, half});
        OreGroup g{true, (lo + hi) * 0.5, -1, F.count(), 0};          // zone by the orbit-gap midpoint
        const OreTable bt = zoneOreTable(ores, oz, g.distance, &g.zone);
        uint32_t noiseSeed = mixSeed(gp.seed, 900 + b);
        for (int n = 0, tries = 0; n < gp.beltAsteroids && tries < gp.beltAsteroids * 30; tries++) {
            double ang = rng.range(0.0f, 6.2831853f);
            double r = inner + (outer - inner) * rng.f();
            float dens = fbm((float)std::cos(ang) * 2.0f, (float)std::sin(ang) * 2.0f, (float)(r / 15000.0), noiseSeed, 3);   // patchy, not uniform
            if (rng.f() > 0.25f + 0.75f * dens) continue;
            double h = half * (rng.f() + rng.f() - 1.0);                      // triangular: thickest in the middle
            detail::pushAsteroid(F, rng, {sun.x + r * std::cos(ang), sun.y + h, sun.z + r * std::sin(ang)}, detail::beltRadius(rng), bt);
            n++;
        }
        g.count = F.count() - g.first;
        if (groupsOut) groupsOut->push_back(g);
    }

    auto sunDist = [&sun](const Vec3d& p) { double dx = p.x - sun.x, dy = p.y - sun.y, dz = p.z - sun.z; return std::sqrt(dx * dx + dy * dy + dz * dz); };
    // shell cluster: a flattened shell around a planet (outside its moons' orbits) or a moon
    auto shell = [&](int ti) {
        const TargetBody& t = targets[ti];
        double inner = t.moon ? t.radius * 3.0 + 100.0 : t.radius * 4.5 + 500.0;
        double width = std::max(400.0, t.radius * 3.0);
        OreGroup g{false, sunDist(t.pos), -1, F.count(), 0, false, ti};   // zone by the body's distance from the sun
        const OreTable ct = zoneOreTable(ores, oz, g.distance, &g.zone);
        for (int n = 0; n < gp.clusterAsteroids; n++) {
            float dx, dy, dz, l;
            do { dx = rng.range(-1, 1); dy = rng.range(-1, 1); dz = rng.range(-1, 1); l = std::sqrt(dx * dx + dy * dy + dz * dz); } while (l > 1.0f || l < 0.1f);
            double r = inner + width * rng.f();
            detail::pushAsteroid(F, rng, {t.pos.x + dx / l * r, t.pos.y + dy / l * r * 0.5, t.pos.z + dz / l * r}, detail::clusterRadius(rng), ct);   // flattened shell
        }
        g.count = F.count() - g.first;
        if (groupsOut) groupsOut->push_back(g);
    };
    // ring: a flat band around a planet in the XZ plane through its centre (same shape as a belt: angle + radius band + thin height)
    auto ring = [&](int ti) {
        const TargetBody& t = targets[ti];
        double inner = t.radius * (double)rng.range((float)kRingMinInner, (float)kRingMaxInner);
        double outer = std::min(t.radius * kRingMaxOuter, inner + t.radius * (double)rng.range(0.3f, 0.5f));
        double half = ringHalfHeight(t.radius);
        OreGroup g{false, sunDist(t.pos), -1, F.count(), 0, true, ti};
        const OreTable rt = zoneOreTable(ores, oz, g.distance, &g.zone);
        for (int n = 0; n < gp.ringAsteroids; n++) {
            double ang = rng.range(0.0f, 6.2831853f);
            double r = inner + (outer - inner) * rng.f();
            double h = half * (rng.f() + rng.f() - 1.0);
            detail::pushAsteroid(F, rng, {t.pos.x + r * std::cos(ang), t.pos.y + h, t.pos.z + r * std::sin(ang)}, detail::clusterRadius(rng), rt);
        }
        g.count = F.count() - g.first;
        if (groupsOut) groupsOut->push_back(g);
    };

    // planets: every planet gets a ring (seeded per-planet chance) or else a shell cluster; moons never compete for these
    std::vector<int> moons;
    for (int i = 0, planet = 0; i < (int)targets.size(); i++) {
        if (targets[i].moon) { moons.push_back(i); continue; }
        if (planetHasRing(gp.seed, planet++, gp.ringChance)) ring(i);
        else if (gp.planetShells) shell(i);
    }
    // moons: their own smaller budget, seeded shuffle
    for (int i = (int)moons.size() - 1; i > 0; i--) std::swap(moons[i], moons[rng.irange(0, i)]);
    int moonClusters = std::min<int>(std::max(0, gp.moonClusterCount), (int)moons.size());
    for (int c = 0; c < moonClusters; c++) shell(moons[c]);
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

// ---- health ----
// Hit points grow with the surface: k * radius^2 (radius 9 with the default k = 1.25: about 100 HP: ten blaster bolts of 10, or 3-4 s of mining beam at 30 per second).
inline float asteroidMaxHp(float radius, float hpScale) { return std::max(1.0f, hpScale * radius * radius); }
// Applies damage; returns true when this hit takes the asteroid to 0 (a destroyed one, hp <= 0, is never "destroyed" again). Non-positive damage does nothing.
inline bool applyAsteroidDamage(float& hp, float amount) {
    if (hp <= 0.0f || amount <= 0.0f) return false;
    hp -= amount;
    if (hp <= 0.0f) { hp = 0.0f; return true; }
    return false;
}

// The n indices nearest to p, nearest first. `alive` (optional, one byte per asteroid) hides destroyed ones.
inline void nearestIndices(const AsteroidField& F, const Vec3d& p, int n, std::vector<int>& out, const std::vector<uint8_t>* alive = nullptr) {
    out.clear();
    if (n <= 0) return;
    std::vector<std::pair<double, int>> d;
    d.reserve(F.count());
    for (int i = 0; i < F.count(); i++) {
        if (alive && (size_t)i < alive->size() && !(*alive)[i]) continue;
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
