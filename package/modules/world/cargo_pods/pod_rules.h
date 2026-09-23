#pragma once
// Pure cargo pod rules: no GL, no SDL, no engine services. Unit-tested in package/tests/test_cargo_pods.cpp. Design: docs/CARGO_PODS.md.
// A pool of N independent slots, each running its own seeded Waiting -> Active -> (collected | expired) -> Waiting cycle; a seeded site pick
// (the distress-beacon / anomaly zones: deep space between two orbits / near a body / in the belt); a slow seeded drift; the magnet + scoop
// pickup (the SAME shape as gameplay/mining's capsules); and a weighted ore + optional crafted-item reward roll.
//
// The zone pick, the magnet step and weightedPick are trimmed COPIES of world/distress_beacons' beacon_rules.h, gameplay/mining's
// mining_rules.h and world/anomalies' anomaly_rules.h, not includes: those live in other optional modules, and including their rules
// headers would mean deleting one of them breaks this module's build (VISION.md's modularity rule). Only world/star_system headers (a
// required dependency) are used.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "world/star_system/star_system_api.h"     // world::Vec3d, BodyKind
#include "world/star_system/planet_mesh.h"         // mixSeed
#include "world/star_system/star_system_rules.h"   // detail::Rng

namespace world {

struct PodParams {
    int poolSize = 6;                             // independent pod slots
    double respawnMin = 60, respawnMax = 240;     // seconds an emptied slot waits before spawning again (rolled per cycle)
    double lifetime = 900;                        // seconds an unclaimed pod lasts before it drifts off (and the slot re-rolls elsewhere); 0 = forever
    int oreMin = 2, oreMax = 6;                   // ore units per pod (a mining capsule is ~40, a distress beacon 10 + a black box)
    double itemChance = 0.15;                     // 0..1: chance of one bonus crafted item on top of the ore
    double drift = 2.0;                           // m/s: the fastest a pod's seeded drift can be
    // pickup: the mining capsule feel (the module reads mining.magnet_radius / magnet_speed / scoop_radius / scoop_speed)
    double magnetRadius = 120, magnetSpeed = 140, scoopRadius = 20, scoopSpeed = 0;
};

constexpr int kPodPoolMax = 32;

// Clamps nonsense tunables into something that runs. NaN falls back to the defaults.
inline PodParams sanitizePodParams(PodParams p) {
    const PodParams d;
    auto fin = [](double v, double def) { return std::isfinite(v) ? v : def; };
    p.poolSize = std::clamp(p.poolSize, 0, kPodPoolMax);
    p.respawnMin = std::max(1.0, fin(p.respawnMin, d.respawnMin));
    p.respawnMax = std::max(p.respawnMin, fin(p.respawnMax, d.respawnMax));
    p.lifetime = std::max(0.0, fin(p.lifetime, d.lifetime));
    p.oreMin = std::clamp(p.oreMin, 0, 1000);
    p.oreMax = std::clamp(p.oreMax, p.oreMin, 1000);
    p.itemChance = std::clamp(fin(p.itemChance, d.itemChance), 0.0, 1.0);
    p.drift = std::clamp(fin(p.drift, d.drift), 0.0, 100.0);
    p.magnetRadius = std::max(1.0, fin(p.magnetRadius, d.magnetRadius));
    p.magnetSpeed = std::max(10.0, fin(p.magnetSpeed, d.magnetSpeed));
    p.scoopRadius = std::max(0.5, fin(p.scoopRadius, d.scoopRadius));
    p.scoopSpeed = std::max(0.0, fin(p.scoopSpeed, d.scoopSpeed));
    return p;
}

// ---- seeds / intervals ----
inline uint32_t podSlotSeed(uint32_t seed, int slot) { return mixSeed(seed ^ 0xCA2600u, (uint32_t)slot); }
inline uint32_t podSiteSeed(uint32_t slotSeed, uint32_t cycle) { return mixSeed(slotSeed ^ 0x9D5173u, cycle); }

// Seconds a slot waits before pod number `cycle` spawns. Cycle 0 (boot) is uniform in [0, respawnMin) so the field fills early and the slots
// are staggered; later cycles are uniform in [respawnMin, respawnMax]. Same (slotSeed, cycle) -> same value.
inline double podInterval(uint32_t slotSeed, uint32_t cycle, double respawnMin, double respawnMax) {
    if (respawnMax < respawnMin) std::swap(respawnMin, respawnMax);
    const double u = (double)mixSeed(slotSeed ^ 0x7153u, cycle) / 4294967296.0;   // [0, 1)
    if (cycle == 0) return respawnMin * u;
    return respawnMin + (respawnMax - respawnMin) * u;
}

// ---- site pick (trimmed copy of world/distress_beacons' beaconCandidates, itself the world/anomalies zone logic) ----
enum class PodZone { DeepSpace, NearBody, Belt };

struct PodSite {
    PodZone zone = PodZone::DeepSpace;
    int anchor = -1;               // body id it moves with (NearBody), -1 = a fixed world position
    Vec3d offset;                  // world position (anchor -1) or offset from the anchor body's centre
};

struct PodBody { int id = 0; BodyKind kind = BodyKind::Planet; float radius = 1; double orbitRadius = 0; int parent = -1; };
struct PodStation { int parent = -1; double offsetDist = 0; };

struct PodWorld {                  // what the site pick needs to know (from IStarSystem / IStations / IAsteroids)
    Vec3d sun;
    std::vector<PodBody> bodies;          // index 0 = the sun
    std::vector<PodStation> stations;
    std::vector<Vec3d> belt;              // a sample of asteroid positions (empty = no belt sites)
};

constexpr double kPodBodyMargin = 150.0;         // same margins as world/anomalies and world/distress_beacons
constexpr double kPodStationMargin = 400.0;

namespace pdetail {
inline Vec3d unitDir(detail::Rng& rng) {
    for (int i = 0; i < 16; i++) {
        double x = rng.range(-1, 1), y = rng.range(-1, 1), z = rng.range(-1, 1), l = std::sqrt(x * x + y * y + z * z);
        if (l > 0.2 && l <= 1.0) return {x / l, y / l, z / l};
    }
    return {1, 0, 0};
}
inline Vec3d add(Vec3d a, Vec3d b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3d sub(Vec3d a, Vec3d b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3d mul(Vec3d a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double len(Vec3d a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
inline double dist(Vec3d a, Vec3d b) { return len(sub(a, b)); }

inline double nearBodyDistance(const PodWorld& w, const PodBody& b, detail::Rng& rng) {
    for (int attempt = 0; attempt < 8; attempt++) {
        double d = b.radius * rng.range(1.6f, 3.0f) + kPodBodyMargin;
        bool ok = true;
        for (const PodStation& s : w.stations) if (s.parent == b.id && std::fabs(d - s.offsetDist) < kPodStationMargin) ok = false;
        for (const PodBody& m : w.bodies) if (m.parent == b.id && std::fabs(d - m.orbitRadius) < m.radius * 2.0 + kPodBodyMargin) ok = false;
        if (b.kind == BodyKind::Moon && b.parent >= 0)
            for (const PodBody& par : w.bodies) if (par.id == b.parent && d > b.orbitRadius - par.radius - kPodBodyMargin) ok = false;
        if (ok) return d;
    }
    return -1;
}

// weightedPick (copy of world/anomalies'): index by weight, u in [0, 1). All-zero weights: uniform. -1 only for n <= 0.
template <class W>
inline int weightedPick(int n, W weightOf, float u) {
    if (n <= 0) return -1;
    double total = 0;
    for (int i = 0; i < n; i++) total += std::max(0.0f, weightOf(i));
    if (total <= 0) return std::clamp((int)(u * (float)n), 0, n - 1);
    double t = (double)u * total;
    for (int i = 0; i < n; i++) { t -= std::max(0.0f, weightOf(i)); if (t < 0) return i; }
    for (int i = n - 1; i >= 0; i--) if (weightOf(i) > 0) return i;
    return n - 1;
}
} // namespace pdetail

// One candidate per zone (DeepSpace, NearBody, Belt; no belt: the first two), in that order. A zone with no spot falls back to DeepSpace.
inline std::vector<PodSite> podCandidates(const PodWorld& w, uint32_t siteSeed) {
    std::vector<PodSite> out;
    detail::Rng rng(siteSeed);
    std::vector<double> rings;
    const float sunR = w.bodies.empty() ? 1000.0f : w.bodies[0].radius;
    rings.push_back(sunR * 5.0);
    for (const PodBody& b : w.bodies) if (b.kind == BodyKind::Planet) rings.push_back(b.orbitRadius);
    std::sort(rings.begin(), rings.end());
    if (rings.size() < 2) rings.push_back(rings.back() + 30000.0);
    std::vector<const PodBody*> nearCandidates;
    for (const PodBody& b : w.bodies) if (b.kind != BodyKind::Sun) nearCandidates.push_back(&b);
    const int zones = w.belt.empty() ? 2 : 3;
    for (int i = 0; i < zones; i++) {
        PodSite s;
        const PodZone z = (PodZone)i;
        bool placed = false;
        if (z == PodZone::NearBody && !nearCandidates.empty()) {
            const PodBody* b = nearCandidates[(size_t)rng.irange(0, (int)nearCandidates.size() - 1)];
            const double d = pdetail::nearBodyDistance(w, *b, rng);
            if (d > 0) { s.zone = z; s.anchor = b->id; s.offset = pdetail::mul(pdetail::unitDir(rng), d); placed = true; }
        } else if (z == PodZone::Belt) {
            const Vec3d a = w.belt[(size_t)rng.irange(0, (int)w.belt.size() - 1)];
            s.zone = z; s.offset = pdetail::add(a, pdetail::mul(pdetail::unitDir(rng), rng.range(150.0f, 400.0f))); placed = true;
        }
        if (!placed) {
            const int gap = rng.irange(0, (int)rings.size() - 2);
            const double r = rings[(size_t)gap] + (rings[(size_t)gap + 1] - rings[(size_t)gap]) * rng.range(0.35f, 0.65f);
            const double a = rng.range(0.0f, 6.2831853f);
            s.zone = PodZone::DeepSpace;
            s.offset = pdetail::add(w.sun, {std::cos(a) * r, r * rng.range(-0.04f, 0.04f), std::sin(a) * r});
        }
        out.push_back(s);
    }
    return out;
}

inline Vec3d podPosition(const PodSite& s, const Vec3d& anchorPos) { return s.anchor >= 0 ? pdetail::add(anchorPos, s.offset) : s.offset; }

// Picks one candidate: a seeded zone, but never one within `minClear` of the ship (a pod never appears inside the magnet); the next zone in
// order is tried instead, and if every candidate is that close, the farthest. -1 only for an empty list.
inline int pickPodCandidate(const std::vector<Vec3d>& positions, const Vec3d& ship, double minClear, uint32_t siteSeed) {
    if (positions.empty()) return -1;
    const int n = (int)positions.size(), start = (int)(mixSeed(siteSeed, 0x2071u) % (uint32_t)n);
    int far = 0;
    double best = -1;
    for (int k = 0; k < n; k++) {
        const int i = (start + k) % n;
        const double d = pdetail::dist(positions[(size_t)i], ship);
        if (d > minClear) return i;
        if (d > best) { best = d; far = i; }
    }
    return far;
}

// A slow seeded drift velocity (m/s, any direction, length in [0.25, 1] x maxSpeed). maxSpeed <= 0: none.
inline Vec3d podDrift(uint32_t siteSeed, double maxSpeed) {
    if (!(maxSpeed > 0)) return {};
    detail::Rng rng(mixSeed(siteSeed, 0xD21F7u));
    return pdetail::mul(pdetail::unitDir(rng), maxSpeed * rng.range(0.25f, 1.0f));
}

// ---- reward (deterministic per site) ----
struct PodReward { int ore = -1; int amount = 0; int item = -1; };   // indices into the caller's ore / item lists; -1 = none

// Ore: a weighted pick (weights = data/ores.json rarity: common ores common) and a uniform amount in [oreMin, oreMax]. Item: with
// itemChance, one uniform pick from itemCount items. Same siteSeed -> same reward.
inline PodReward rollPodReward(uint32_t siteSeed, const std::vector<float>& oreWeights, int oreMin, int oreMax, double itemChance, int itemCount) {
    PodReward r;
    detail::Rng rng(mixSeed(siteSeed, 0x2E3A2Du));
    const float uOre = rng.f(), uItem = rng.f(), uPick = rng.f();
    if (oreMax < oreMin) std::swap(oreMin, oreMax);
    const int amount = rng.irange(std::max(0, oreMin), std::max(0, oreMax));
    r.ore = pdetail::weightedPick((int)oreWeights.size(), [&](int i) { return oreWeights[(size_t)i]; }, uOre);
    r.amount = r.ore >= 0 ? amount : 0;
    if (r.amount <= 0) r.ore = -1;
    if (itemCount > 0 && (double)uItem < itemChance) r.item = std::clamp((int)(uPick * (float)itemCount), 0, itemCount - 1);
    return r;
}

// ---- pickup (the gameplay/mining capsule feel) ----
// One step of a pod's motion relative to its frame: inside magnetRadius (and not blocked) its velocity eases toward the ship at a speed that
// grows with distance (never slower than 40 m/s, at most maxSpeed); outside it keeps its drift. Returns the new velocity; the caller moves it.
inline Vec3d podMagnetVelocity(const Vec3d& vel, const Vec3d& drift, const Vec3d& pos, const Vec3d& ship, double dt, double magnetRadius,
                               double maxSpeed, bool blocked) {
    if (blocked) return {};
    const Vec3d to = pdetail::sub(ship, pos);
    const double d = pdetail::len(to);
    const double k = 1.0 - std::exp(-6.0 * std::max(0.0, dt));   // eases in: no jerk when the magnet catches it
    Vec3d want = drift;
    if (d <= magnetRadius && d > 1e-6) want = pdetail::mul(to, std::clamp(d * 2.5 + 40.0, 40.0, std::max(40.0, maxSpeed)) / d);
    return pdetail::add(vel, pdetail::mul(pdetail::sub(want, vel), k));
}

// Close enough, and (scoopSpeed > 0 only) slow enough relative to the pod. scoopSpeed <= 0 = collected on contact at any speed.
inline bool podCanScoop(double distance, double relativeSpeed, double scoopRadius, double scoopSpeed) {
    return distance >= 0 && distance <= scoopRadius && (scoopSpeed <= 0.0 || relativeSpeed <= scoopSpeed);
}

// ---- the pool of slots ----
struct PodSlot {
    uint32_t seed = 0;               // podSlotSeed(pool seed, index)
    uint32_t cycle = 0;              // which pod this slot is on (interval and site rolls' input)
    bool active = false;
    double untilSpawn = 0;           // seconds (Waiting)
    double lifeLeft = 0;             // seconds (Active; ignored when lifetime is 0)
};

inline std::vector<PodSlot> startPodPool(uint32_t seed, const PodParams& p) {
    std::vector<PodSlot> out((size_t)std::clamp(p.poolSize, 0, kPodPoolMax));
    for (size_t i = 0; i < out.size(); i++) {
        out[i].seed = podSlotSeed(seed, (int)i);
        out[i].untilSpawn = podInterval(out[i].seed, 0, p.respawnMin, p.respawnMax);
    }
    return out;
}

inline void endPod(PodSlot& s, const PodParams& p) {
    s.active = false;
    s.lifeLeft = 0;
    s.cycle++;
    s.untilSpawn = podInterval(s.seed, s.cycle, p.respawnMin, p.respawnMax);
}

struct PodStep { bool spawned = false, expired = false; };

// One slot, one step. Waiting counts down and spawns (pick a site from podSiteSeed(seed, cycle)); Active counts its lifetime down (unless
// lifetime is 0) and expires into Waiting with a fresh roll. A huge step still leaves a new pod live for one step.
inline PodStep stepPodSlot(PodSlot& s, const PodParams& p, double dt) {
    PodStep out;
    if (!(dt > 0)) return out;
    if (!s.active) {
        s.untilSpawn -= dt;
        if (s.untilSpawn > 0) return out;
        s.active = true;
        s.lifeLeft = p.lifetime > 0 ? std::max(1e-3, p.lifetime - std::max(0.0, -s.untilSpawn)) : 0;
        s.untilSpawn = 0;
        out.spawned = true;
        return out;
    }
    if (p.lifetime <= 0) return out;
    s.lifeLeft -= dt;
    if (s.lifeLeft <= 0) { out.expired = true; endPod(s, p); }
    return out;
}

// The ship emptied the pod: the slot starts its next wait now. False (nothing changes) when the slot has no pod.
inline bool collectPod(PodSlot& s, const PodParams& p) {
    if (!s.active) return false;
    endPod(s, p);
    return true;
}

inline int activePods(const std::vector<PodSlot>& slots) {
    int n = 0;
    for (const PodSlot& s : slots) n += s.active ? 1 : 0;
    return n;
}

} // namespace world
