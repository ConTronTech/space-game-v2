#pragma once
// Pure anomaly rules: no GL, no SDL, no engine services. Unit-tested in package/tests/test_anomalies.cpp. Design: docs/ANOMALIES.md.
// Data parsing (data/anomalies.json), seeded site generation, detect / investigate range tests, the per-site reward and the saved id list.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "engine/json.h"
#include "world/star_system/star_system_api.h"     // world::Vec3d, BodyKind
#include "world/star_system/planet_mesh.h"         // mixSeed
#include "world/star_system/star_system_rules.h"   // detail::Rng (the same deterministic generator the star system uses)

namespace world {

// ---- data: one anomaly KIND (data/anomalies.json entry) ----
struct AnomalyReward { std::string ore; int min = 1, max = 1; float weight = 1; };
struct AnomalyKind {
    std::string id, name;
    std::vector<std::string> messages;     // flavour text variants; one is picked per site (seeded)
    std::vector<AnomalyReward> rewards;    // one entry is picked per site (weighted), then an amount in [min, max]
    float detectMul = 1.0f;                // x anomalies.detect_range: "louder" kinds are seen from farther away (0.1 .. 5)
    float weight = 1.0f;                   // how often generation picks this kind (>= 0; all zero = uniform)
};

inline AnomalyKind anomalyKindFromJson(const std::string& id, const engine::Json& j) {
    AnomalyKind k;
    k.id = id;
    k.name = j["name"].str(id);
    if (k.name.empty()) k.name = id;
    const engine::Json& m = j["messages"];
    for (size_t i = 0; i < m.size(); i++) if (!m.at(i).str().empty()) k.messages.push_back(m.at(i).str());
    if (k.messages.empty() && !j["message"].str().empty()) k.messages.push_back(j["message"].str());
    if (k.messages.empty()) k.messages.push_back("Anomaly investigated.");
    const engine::Json& r = j["rewards"];
    for (size_t i = 0; i < r.size(); i++) {
        AnomalyReward w;
        w.ore = r.at(i)["ore"].str();
        if (w.ore.empty()) continue;
        w.min = std::clamp((int)r.at(i)["min"].num(1), 1, 1000);
        w.max = std::clamp((int)r.at(i)["max"].num(w.min), w.min, 1000);
        w.weight = std::max(0.0f, (float)r.at(i)["weight"].num(1));
        k.rewards.push_back(w);
    }
    float dm = (float)j["detect_mult"].num(1.0);
    k.detectMul = std::isfinite(dm) ? std::clamp(dm, 0.1f, 5.0f) : 1.0f;
    float w = (float)j["weight"].num(1.0);
    k.weight = std::isfinite(w) ? std::max(0.0f, w) : 1.0f;
    return k;
}

// Weighted pick in [0, n): weights <= 0 are never picked unless every weight is <= 0 (then uniform). u in [0, 1).
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

// ---- generation ----
enum class SiteZone { DeepSpace, NearBody, Belt };

struct AnomalySite {
    int id = 0;                    // index in the generated list: stable for a given seed (what the save stores)
    int kind = 0;                  // index into the kind table
    SiteZone zone = SiteZone::DeepSpace;
    int anchor = -1;               // body id it moves with (NearBody), -1 = a fixed world position (DeepSpace, Belt)
    Vec3d offset;                  // world position (anchor -1) or offset from the anchor body's centre
    uint32_t seed = 0;             // per-site seed: reward and flavour text are rolled from this, never per frame
};

struct GenBody {                   // what generation needs to know about a body (from IStarSystem::bodies())
    int id = 0;
    BodyKind kind = BodyKind::Planet;
    float radius = 1;
    double orbitRadius = 0;
    int parent = -1;
};
struct GenStation { int parent = -1; double offsetDist = 0; };   // a station's distance from its parent planet's centre

struct AnomalyGenParams {
    unsigned seed = 1234;          // world.seed: the same seed gives the same layout
    int count = 8;
    Vec3d sun;                     // the sun's (fixed) world position
    std::vector<GenBody> bodies;   // index 0 = the sun
    std::vector<GenStation> stations;
    std::vector<Vec3d> belt;       // a sample of asteroid positions (empty = no belt sites)
    std::vector<float> kindWeights;// one per kind (size = kind count, >= 1)
};

constexpr double kAnomalyBodyMargin = 150.0;      // units: a near-body site is at least this far outside the surface (and any station/moon ring)
constexpr double kAnomalyStationMargin = 400.0;

namespace adetail {
inline Vec3d unitDir(detail::Rng& rng, float ySpread = 1.0f) {
    for (int i = 0; i < 16; i++) {
        double x = rng.range(-1, 1), y = rng.range(-ySpread, ySpread), z = rng.range(-1, 1), l = std::sqrt(x * x + y * y + z * z);
        if (l > 0.2 && l <= 1.0) return {x / l, y / l, z / l};
    }
    return {1, 0, 0};
}
inline Vec3d add(Vec3d a, Vec3d b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3d mul(Vec3d a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dist(Vec3d a, Vec3d b) { double x = a.x - b.x, y = a.y - b.y, z = a.z - b.z; return std::sqrt(x * x + y * y + z * z); }
} // namespace adetail

// Near-body offset distance for body b, clear of its stations' and moons' rings. Returns < 0 when no clear spot was found.
inline double nearBodyDistance(const AnomalyGenParams& p, const GenBody& b, detail::Rng& rng) {
    for (int attempt = 0; attempt < 8; attempt++) {
        double d = b.radius * rng.range(1.6f, 3.0f) + kAnomalyBodyMargin;
        bool ok = true;
        for (const GenStation& s : p.stations) if (s.parent == b.id && std::fabs(d - s.offsetDist) < kAnomalyStationMargin) ok = false;
        for (const GenBody& m : p.bodies) if (m.parent == b.id && std::fabs(d - m.orbitRadius) < m.radius * 2.0 + kAnomalyBodyMargin) ok = false;
        if (b.kind == BodyKind::Moon && b.parent >= 0) {           // a moon's site must stay well inside its orbit around the planet
            for (const GenBody& par : p.bodies) if (par.id == b.parent && d > b.orbitRadius - par.radius - kAnomalyBodyMargin) ok = false;
        }
        if (ok) return d;
    }
    return -1;
}

// Deterministic for a given params (seed, bodies, belt sample). Zones rotate DeepSpace -> NearBody -> Belt (no belt: DeepSpace/NearBody);
// a zone that finds no spot falls back to DeepSpace. Kinds are weighted by AnomalyKind::weight.
inline std::vector<AnomalySite> generateAnomalies(const AnomalyGenParams& p) {
    std::vector<AnomalySite> out;
    const int n = std::clamp(p.count, 0, 64);
    if (n == 0 || p.kindWeights.empty()) return out;
    detail::Rng rng(mixSeed(p.seed, 0xA40A11u));
    std::vector<double> rings;                                         // planet orbit radii, sorted: deep sites sit between them
    float sunR = p.bodies.empty() ? 1000.0f : p.bodies[0].radius;
    rings.push_back(sunR * 5.0);
    for (const GenBody& b : p.bodies) if (b.kind == BodyKind::Planet) rings.push_back(b.orbitRadius);
    std::sort(rings.begin(), rings.end());
    if (rings.size() < 2) rings.push_back(rings.back() + 30000.0);
    std::vector<int> nearCandidates;
    for (const GenBody& b : p.bodies) if (b.kind != BodyKind::Sun) nearCandidates.push_back(b.id);
    const int zones = p.belt.empty() ? 2 : 3;
    for (int i = 0; i < n; i++) {
        AnomalySite s;
        s.id = i;
        s.kind = weightedPick((int)p.kindWeights.size(), [&](int k) { return p.kindWeights[(size_t)k]; }, rng.f());
        s.seed = mixSeed(p.seed ^ 0x5EEDA0u, (uint32_t)i);
        SiteZone z = (SiteZone)(i % zones);
        bool placed = false;
        if (z == SiteZone::NearBody && !nearCandidates.empty()) {
            const GenBody* b = nullptr;
            int want = nearCandidates[(size_t)rng.irange(0, (int)nearCandidates.size() - 1)];
            for (const GenBody& g : p.bodies) if (g.id == want) b = &g;
            double d = b ? nearBodyDistance(p, *b, rng) : -1;
            if (d > 0) { s.zone = z; s.anchor = b->id; s.offset = adetail::mul(adetail::unitDir(rng), d); placed = true; }
        } else if (z == SiteZone::Belt) {
            Vec3d a = p.belt[(size_t)rng.irange(0, (int)p.belt.size() - 1)];
            s.zone = z; s.offset = adetail::add(a, adetail::mul(adetail::unitDir(rng), rng.range(150.0f, 400.0f))); placed = true;
        }
        if (!placed) {                                                 // deep space: between two adjacent planet orbits, near the orbital plane
            int gap = rng.irange(0, (int)rings.size() - 2);
            double r = rings[(size_t)gap] + (rings[(size_t)gap + 1] - rings[(size_t)gap]) * rng.range(0.35f, 0.65f);
            double a = rng.range(0.0f, 6.2831853f);
            s.zone = SiteZone::DeepSpace;
            s.offset = adetail::add(p.sun, {std::cos(a) * r, r * rng.range(-0.04f, 0.04f), std::sin(a) * r});
        }
        out.push_back(s);
    }
    return out;
}

// Current world position: anchor body position (from the caller) + offset, or the fixed position.
inline Vec3d sitePosition(const AnomalySite& s, const Vec3d& anchorPos) { return s.anchor >= 0 ? adetail::add(anchorPos, s.offset) : s.offset; }

// ---- detection / investigation ----
inline float anomalyDetectRange(float detectRange, float kindMul) { return std::max(0.0f, detectRange) * std::clamp(kindMul, 0.1f, 5.0f); }
// Only with the scanner perk; an investigated site is never detected again. False for NaN distances.
inline bool anomalyDetected(double dist, float detectRange, float kindMul, bool perk, bool investigated) {
    return perk && !investigated && dist >= 0 && dist <= (double)anomalyDetectRange(detectRange, kindMul);
}
inline bool anomalyInvestigates(double dist, float investigateRange, bool perk, bool investigated) {
    return perk && !investigated && dist >= 0 && dist <= (double)investigateRange;
}

// ---- reward (deterministic per site) ----
struct AnomalyRoll { std::string ore; int amount = 0; int message = 0; };
inline AnomalyRoll rollAnomaly(const AnomalyKind& k, uint32_t siteSeed) {
    AnomalyRoll r;
    detail::Rng rng(siteSeed);
    r.message = k.messages.empty() ? 0 : rng.irange(0, (int)k.messages.size() - 1);
    int w = weightedPick((int)k.rewards.size(), [&](int i) { return k.rewards[(size_t)i].weight; }, rng.f());
    if (w >= 0) { const AnomalyReward& rw = k.rewards[(size_t)w]; r.ore = rw.ore; r.amount = rng.irange(rw.min, rw.max); }
    return r;
}

// ---- save: which ids are done ----
inline std::vector<int> investigatedIds(const std::vector<bool>& done) {
    std::vector<int> ids;
    for (size_t i = 0; i < done.size(); i++) if (done[i]) ids.push_back((int)i);
    return ids;
}
// Out-of-range ids (a save from a different seed / count) and duplicates are ignored.
inline std::vector<bool> investigatedMask(const std::vector<int>& ids, int count) {
    std::vector<bool> m((size_t)std::max(0, count), false);
    for (int id : ids) if (id >= 0 && id < count) m[(size_t)id] = true;
    return m;
}

} // namespace world
