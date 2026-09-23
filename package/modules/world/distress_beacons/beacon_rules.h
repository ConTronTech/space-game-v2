#pragma once
// Pure distress beacon rules: no GL, no SDL, no engine services. Unit-tested in package/tests/test_distress_beacons.cpp. Design: docs/DISTRESS_BEACONS.md.
// Seeded spawn interval, seeded site pick (the world/anomalies zone idea: deep space between two orbits / near a body / in the belt), the
// lifetime countdown, detect / investigate range tests, and the whole Waiting -> Active -> (claimed | expired) -> Waiting cycle as one step.
//
// The zone pick is a trimmed DUPLICATE of world/anomalies' generateAnomalies, not an include of anomaly_rules.h: that header lives in another
// optional module, and including it would mean deleting world/anomalies breaks this module's build (VISION.md's modularity rule). Only
// world/star_system headers (a required dependency) are used.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "world/star_system/star_system_api.h"     // world::Vec3d, BodyKind
#include "world/star_system/planet_mesh.h"         // mixSeed
#include "world/star_system/star_system_rules.h"   // detail::Rng

namespace world {

struct BeaconParams {
    double intervalMin = 60, intervalMax = 180;   // seconds between one beacon ending (claimed or expired) and the next spawning (rolled per cycle)
    double lifetime = 120;                        // seconds an unclaimed beacon lasts
    double detectRange = 150000;                  // units: shown on the radar within this (no perk: it is a broadcast)
    double investigateRange = 75;                 // units: flying this close claims it
};

// Clamps nonsense tunables into something that runs. NaN falls back to the defaults.
inline BeaconParams sanitizeBeaconParams(BeaconParams p) {
    const BeaconParams d;
    auto fin = [](double v, double def) { return std::isfinite(v) ? v : def; };
    p.intervalMin = std::max(1.0, fin(p.intervalMin, d.intervalMin));
    p.intervalMax = std::max(p.intervalMin, fin(p.intervalMax, d.intervalMax));
    p.lifetime = std::max(1.0, fin(p.lifetime, d.lifetime));
    p.detectRange = std::max(0.0, fin(p.detectRange, d.detectRange));
    p.investigateRange = std::max(1.0, fin(p.investigateRange, d.investigateRange));
    return p;
}

// ---- seeded interval / site seed ----
// Seconds until beacon number `cycle` spawns, uniform in [intervalMin, intervalMax]. Same (seed, cycle) -> same value.
inline double beaconInterval(uint32_t seed, uint32_t cycle, double intervalMin, double intervalMax) {
    if (intervalMax < intervalMin) std::swap(intervalMin, intervalMax);
    const double u = (double)mixSeed(seed ^ 0xBEAC0Du, cycle) / 4294967296.0;   // [0, 1)
    return intervalMin + (intervalMax - intervalMin) * u;
}
inline uint32_t beaconSiteSeed(uint32_t seed, uint32_t cycle) { return mixSeed(seed ^ 0xD15735u, cycle); }

// ---- site pick (trimmed copy of the world/anomalies zone logic) ----
enum class BeaconZone { DeepSpace, NearBody, Belt };

struct BeaconSite {
    BeaconZone zone = BeaconZone::DeepSpace;
    int anchor = -1;               // body id it moves with (NearBody), -1 = a fixed world position
    Vec3d offset;                  // world position (anchor -1) or offset from the anchor body's centre
};

struct BeaconBody { int id = 0; BodyKind kind = BodyKind::Planet; float radius = 1; double orbitRadius = 0; int parent = -1; };
struct BeaconStation { int parent = -1; double offsetDist = 0; };   // a station's distance from its parent's centre

struct BeaconWorld {               // what the site pick needs to know (from IStarSystem / IStations / IAsteroids)
    Vec3d sun;
    std::vector<BeaconBody> bodies;       // index 0 = the sun
    std::vector<BeaconStation> stations;
    std::vector<Vec3d> belt;              // a sample of asteroid positions (empty = no belt sites)
};

constexpr double kBeaconBodyMargin = 150.0;      // same margins as world/anomalies
constexpr double kBeaconStationMargin = 400.0;
constexpr int kBeaconCandidates = 6;             // sites rolled per spawn (two per zone); one reachable one is kept

namespace bdetail {
inline Vec3d unitDir(detail::Rng& rng) {
    for (int i = 0; i < 16; i++) {
        double x = rng.range(-1, 1), y = rng.range(-1, 1), z = rng.range(-1, 1), l = std::sqrt(x * x + y * y + z * z);
        if (l > 0.2 && l <= 1.0) return {x / l, y / l, z / l};
    }
    return {1, 0, 0};
}
inline Vec3d add(Vec3d a, Vec3d b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3d mul(Vec3d a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dist(Vec3d a, Vec3d b) { double x = a.x - b.x, y = a.y - b.y, z = a.z - b.z; return std::sqrt(x * x + y * y + z * z); }

// Near-body offset distance clear of the body's stations' and moons' rings (and inside a moon's own orbit). < 0 = no clear spot.
inline double nearBodyDistance(const BeaconWorld& w, const BeaconBody& b, detail::Rng& rng) {
    for (int attempt = 0; attempt < 8; attempt++) {
        double d = b.radius * rng.range(1.6f, 3.0f) + kBeaconBodyMargin;
        bool ok = true;
        for (const BeaconStation& s : w.stations) if (s.parent == b.id && std::fabs(d - s.offsetDist) < kBeaconStationMargin) ok = false;
        for (const BeaconBody& m : w.bodies) if (m.parent == b.id && std::fabs(d - m.orbitRadius) < m.radius * 2.0 + kBeaconBodyMargin) ok = false;
        if (b.kind == BodyKind::Moon && b.parent >= 0)
            for (const BeaconBody& par : w.bodies) if (par.id == b.parent && d > b.orbitRadius - par.radius - kBeaconBodyMargin) ok = false;
        if (ok) return d;
    }
    return -1;
}
} // namespace bdetail

// Deterministic candidate sites for one spawn: zones rotate DeepSpace -> NearBody -> Belt (no belt: DeepSpace/NearBody); a zone that finds no
// spot falls back to DeepSpace (the same rules as world/anomalies' generateAnomalies).
inline std::vector<BeaconSite> beaconCandidates(const BeaconWorld& w, uint32_t siteSeed, int count = kBeaconCandidates) {
    std::vector<BeaconSite> out;
    detail::Rng rng(siteSeed);
    std::vector<double> rings;                                         // planet orbit radii, sorted: deep sites sit between them
    const float sunR = w.bodies.empty() ? 1000.0f : w.bodies[0].radius;
    rings.push_back(sunR * 5.0);
    for (const BeaconBody& b : w.bodies) if (b.kind == BodyKind::Planet) rings.push_back(b.orbitRadius);
    std::sort(rings.begin(), rings.end());
    if (rings.size() < 2) rings.push_back(rings.back() + 30000.0);
    std::vector<const BeaconBody*> nearCandidates;
    for (const BeaconBody& b : w.bodies) if (b.kind != BodyKind::Sun) nearCandidates.push_back(&b);
    const int zones = w.belt.empty() ? 2 : 3;
    for (int i = 0; i < std::clamp(count, 1, 64); i++) {
        BeaconSite s;
        const BeaconZone z = (BeaconZone)(i % zones);
        bool placed = false;
        if (z == BeaconZone::NearBody && !nearCandidates.empty()) {
            const BeaconBody* b = nearCandidates[(size_t)rng.irange(0, (int)nearCandidates.size() - 1)];
            const double d = bdetail::nearBodyDistance(w, *b, rng);
            if (d > 0) { s.zone = z; s.anchor = b->id; s.offset = bdetail::mul(bdetail::unitDir(rng), d); placed = true; }
        } else if (z == BeaconZone::Belt) {
            const Vec3d a = w.belt[(size_t)rng.irange(0, (int)w.belt.size() - 1)];
            s.zone = z; s.offset = bdetail::add(a, bdetail::mul(bdetail::unitDir(rng), rng.range(150.0f, 400.0f))); placed = true;
        }
        if (!placed) {                                                 // deep space: between two adjacent planet orbits, near the orbital plane
            const int gap = rng.irange(0, (int)rings.size() - 2);
            const double r = rings[(size_t)gap] + (rings[(size_t)gap + 1] - rings[(size_t)gap]) * rng.range(0.35f, 0.65f);
            const double a = rng.range(0.0f, 6.2831853f);
            s.zone = BeaconZone::DeepSpace;
            s.offset = bdetail::add(w.sun, {std::cos(a) * r, r * rng.range(-0.04f, 0.04f), std::sin(a) * r});
        }
        out.push_back(s);
    }
    return out;
}

// Current world position: anchor body position (from the caller) + offset, or the fixed position.
inline Vec3d beaconPosition(const BeaconSite& s, const Vec3d& anchorPos) { return s.anchor >= 0 ? bdetail::add(anchorPos, s.offset) : s.offset; }

// Picks one candidate index: a seeded choice among those inside detectRange of the ship but outside investigateRange (a beacon must be
// reachable in its lifetime and never claimed the instant it appears); if none qualify, the nearest one outside investigateRange; if every
// candidate is inside it, 0. `positions` are the candidates' current world positions. -1 only for an empty list.
inline int pickBeaconCandidate(const std::vector<Vec3d>& positions, const Vec3d& ship, double detectRange, double investigateRange, uint32_t siteSeed) {
    if (positions.empty()) return -1;
    std::vector<int> inRange;
    int nearest = -1;
    double best = 0;
    for (size_t i = 0; i < positions.size(); i++) {
        const double d = bdetail::dist(positions[i], ship);
        if (!(d > investigateRange)) continue;
        if (d <= detectRange) inRange.push_back((int)i);
        if (nearest < 0 || d < best) { nearest = (int)i; best = d; }
    }
    if (!inRange.empty()) return inRange[(size_t)(mixSeed(siteSeed, 0x51C4u) % (uint32_t)inRange.size())];
    return nearest >= 0 ? nearest : 0;
}

// ---- detection / investigation ----
inline bool beaconDetected(double dist, double detectRange, bool active) { return active && dist >= 0 && dist <= detectRange; }
inline bool beaconInvestigates(double dist, double investigateRange, bool active) { return active && dist >= 0 && dist <= investigateRange; }

// ---- the whole cycle ----
// Waiting (counting down to the next spawn) -> Active (lifetime counting down) -> Waiting with a freshly rolled interval, either when the
// beacon expires (stepBeacon) or when it is claimed (claimBeacon). At most one beacon at a time.
struct BeaconState {
    uint32_t seed = 1234;
    uint32_t cycle = 0;              // which beacon is next / current (the interval and site rolls' input)
    bool active = false;
    double untilSpawn = 0;           // seconds (Waiting)
    double lifeLeft = 0;             // seconds (Active)
};
inline BeaconState startBeaconCycle(uint32_t seed, const BeaconParams& p, uint32_t cycle = 0) {
    BeaconState s;
    s.seed = seed; s.cycle = cycle;
    s.untilSpawn = beaconInterval(seed, cycle, p.intervalMin, p.intervalMax);
    return s;
}

struct BeaconStep {
    bool spawned = false;            // pick a site from beaconSiteSeed(seed, cycle) and emit DistressBeaconSpawned this step
    bool expired = false;            // emit DistressBeaconExpired this step
};

inline void endBeacon(BeaconState& s, const BeaconParams& p) {
    s.active = false;
    s.lifeLeft = 0;
    s.cycle++;
    s.untilSpawn = beaconInterval(s.seed, s.cycle, p.intervalMin, p.intervalMax);
}

inline BeaconStep stepBeacon(BeaconState& s, const BeaconParams& p, double dt) {
    BeaconStep out;
    if (!(dt > 0)) return out;
    if (!s.active) {
        s.untilSpawn -= dt;
        if (s.untilSpawn > 0) return out;
        s.active = true;
        s.lifeLeft = p.lifetime - std::max(0.0, -s.untilSpawn);   // the overshoot past zero already counts as beacon time
        s.untilSpawn = 0;
        if (s.lifeLeft <= 0) s.lifeLeft = 1e-3;                   // a huge step: still live for one step, so it is seen before it expires
        out.spawned = true;
        return out;
    }
    s.lifeLeft -= dt;
    if (s.lifeLeft <= 0) { out.expired = true; endBeacon(s, p); }
    return out;
}

// The ship reached the active beacon: it is gone, and the next countdown starts now. False (nothing changes) when no beacon is active.
inline bool claimBeacon(BeaconState& s, const BeaconParams& p) {
    if (!s.active) return false;
    endBeacon(s, p);
    return true;
}

// Seconds left on the active beacon, -1 when none is active.
inline double beaconEta(const BeaconState& s) { return s.active ? std::max(0.0, s.lifeLeft) : -1.0; }

} // namespace world
