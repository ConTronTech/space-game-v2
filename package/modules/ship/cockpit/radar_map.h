#pragma once
// Pure maths of the cockpit radar scope (no GL, no services): ship-relative frame, logarithmic range mapping, the
// nearest-N contact list and the distance text. Unit-tested in tests/test_cockpit.cpp.
//
// The plot is top-down and forward-relative: the ship is at the centre, straight ahead is up on the scope, right is right.
// Height above/below the ship's plane is ignored for the position (a body straight above sits at the centre).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include "engine/math.h"

namespace cockpit {

constexpr float kRadarScale = 200.0f;      // units: below this the log mapping is nearly linear
constexpr int kMaxRadarContacts = 20;

struct RadarFrame { engine::Vec3 fwd{0, 0, -1}, right{1, 0, 0}, up{0, 1, 0}; };

// Orthonormal frame from a forward and an up vector. Zero or parallel vectors fall back to world axes instead of NaN.
inline RadarFrame radarFrame(engine::Vec3 fwd, engine::Vec3 up) {
    RadarFrame f;
    fwd = engine::normalize(fwd);
    if (engine::length(fwd) < 0.5f) return f;
    up = engine::normalize(up);
    if (engine::length(up) < 0.5f || std::fabs(engine::dot(fwd, up)) > 0.999f)
        up = std::fabs(fwd.y) < 0.9f ? engine::Vec3{0, 1, 0} : engine::Vec3{0, 0, -1};
    f.fwd = fwd;
    f.right = engine::normalize(engine::cross(fwd, up));
    f.up = engine::cross(f.right, fwd);
    return f;
}

// Distance -> radius on the scope, 0 (centre) .. 1 (rim), logarithmic so 400 units and 40,000 units are both readable.
inline float radarFraction(float distance, float range, float scale = kRadarScale) {
    if (distance <= 0 || range <= 0) return 0;
    return std::clamp(std::log1p(distance / scale) / std::log1p(range / scale), 0.0f, 1.0f);
}

constexpr float kLevelHeight = 25.0f;   // units: closer to the ship's plane than this counts as level (no stem)
constexpr float kLevelSlope = 0.02f;    // ... or closer than this fraction of the distance (~1 degree of elevation)
constexpr float kMaxStem = 0.5f;        // longest stem, as a fraction of the scope radius

// Is a body at this height / distance level with the ship's plane (no stem, no UP/DN label)?
inline bool radarIsLevel(float height, float dist) {
    return !(std::fabs(height) >= std::max(kLevelHeight, kLevelSlope * dist));   // also true for NaN
}

// Vertical offset on the scope for a body 'height' units above (+) / below (-) the ship's plane: the same logarithmic scale as
// the distance rings, scaled to at most maxStem. Level bodies (|height| < kLevelHeight) give exactly 0.
inline float radarHeightOffset(float height, float range, float maxStem = kMaxStem) {
    float a = std::fabs(height);
    if (!(a >= kLevelHeight)) return 0;     // also catches NaN
    return (height > 0 ? 1.0f : -1.0f) * radarFraction(a, range) * maxStem;
}

struct RadarPlot {
    float x = 0, y = 0;      // flat position on the unit disc: x right, y forward
    float dist = 0;          // true 3D distance
    float height = 0;        // signed distance above (+) / below (-) the ship's plane, units
    float stem = 0;          // vertical offset of the contact's dot from (x, y) on the scope, unit-disc units; 0 = level
    bool clamped = false;    // beyond range: sits on the rim
};

// 'rel' = body position minus ship position (subtract in double first, then convert).
inline RadarPlot radarPlot(const engine::Vec3& rel, const RadarFrame& f, float range) {
    RadarPlot p;
    float lx = engine::dot(rel, f.right), lz = engine::dot(rel, f.fwd);
    float h = std::sqrt(lx * lx + lz * lz);
    p.dist = engine::length(rel);
    p.clamped = h > range;
    float r = radarFraction(h, range);
    if (h > 1e-3f) { p.x = lx / h * r; p.y = lz / h * r; }
    p.height = engine::dot(rel, f.up);
    p.stem = radarIsLevel(p.height, p.dist) ? 0.0f : radarHeightOffset(p.height, range);
    // keep the dot inside the scope: shorten the stem if it would poke through the rim (the base is always inside the disc)
    if (p.stem != 0 && p.x * p.x + (p.y + p.stem) * (p.y + p.stem) > 1.0f) {
        float room = std::sqrt(std::max(0.0f, 1.0f - p.x * p.x));
        p.stem = (p.stem > 0 ? room : -room) - p.y;
    }
    return p;
}

struct RadarContact {
    int body = -1;           // index into whatever list the contact came from (body id, station index, asteroid id)
    RadarPlot plot;
};

// The N nearest offered contacts. Fixed storage: no allocation.
template <int N>
struct ContactList {
    RadarContact items[N];
    int count = 0;
    void offer(const RadarContact& c) {
        if (count < N) { items[count++] = c; return; }
        int far = 0;
        for (int i = 1; i < count; i++) if (items[i].plot.dist > items[far].plot.dist) far = i;
        if (c.plot.dist < items[far].plot.dist) items[far] = c;
    }
};
using RadarContacts = ContactList<kMaxRadarContacts>;          // bodies (sun, planets, moons)
constexpr int kMaxRadarStations = 6;
constexpr int kMaxRadarAsteroids = 12;
using RadarStations = ContactList<kMaxRadarStations>;
using RadarAsteroids = ContactList<kMaxRadarAsteroids>;
constexpr int kMaxRadarMarkers = kMaxRadarContacts + kMaxRadarStations + kMaxRadarAsteroids;   // hard cap on everything drawn on the scope

// ---- asteroids: tiny dim dots, only close by ----
constexpr float kDefaultAsteroidRange = 3000.0f;               // units (cockpit.radar_asteroid_range)
inline bool asteroidInRange(float dist, float range) { return dist >= 0 && dist <= range; }   // false for NaN too
inline int asteroidSizeTier(float radius) { return radius < 2.0f ? 0 : radius < 4.0f ? 1 : 2; }   // clusters: 0.5-4 small, 4-10 large
inline float asteroidDotSize(int tier) { return tier <= 0 ? 0.008f : tier == 1 ? 0.011f : 0.015f; }   // scope-height fractions

// ---- stations: a diamond, hollow normally, filled when the dock key would work for THIS station ----
struct StationMarker {
    bool filled = false;
    float size = 0.04f;      // half-diagonal of the diamond, scope-height fractions
};
// dockKnown = ship::IDocking exists and reported a station; dockName / dockOk = what it said (the nearest station, and whether docking works now).
inline StationMarker stationMarker(const std::string& stationName, bool dockKnown, const std::string& dockName, bool dockOk, bool clamped) {
    StationMarker m;
    m.filled = dockKnown && dockOk && stationName == dockName;
    m.size = clamped ? 0.024f : 0.04f;                          // beyond range: a smaller diamond on the rim
    return m;
}

// "Station 1 (Planet 1, orbital)" -> "Station 1": the scope label only has room for the part before the parenthesis.
inline std::string radarShortName(const std::string& name) {
    size_t p = name.find(" (");
    std::string s = p == std::string::npos ? name : name.substr(0, p);
    return s.size() > 18 ? s.substr(0, 18) : s;
}

// "850", "1.5K", "41.9K", "250K", "1.2M". Never negative.
inline std::string radarDistanceText(float d) {
    char b[24];
    d = std::max(d, 0.0f);
    if (d < 1000.0f) std::snprintf(b, sizeof b, "%.0f", d);
    else if (d < 100000.0f) std::snprintf(b, sizeof b, "%.1fK", d / 1000.0f);
    else if (d < 1000000.0f) std::snprintf(b, sizeof b, "%.0fK", d / 1000.0f);
    else std::snprintf(b, sizeof b, "%.1fM", d / 1000000.0f);
    return b;
}

// "UP 800" / "DN 8.1K" for the label; "" when level.
inline std::string radarHeightText(float height, float dist) {
    if (radarIsLevel(height, dist)) return "";
    return std::string(height > 0 ? "UP " : "DN ") + radarDistanceText(std::fabs(height));
}

} // namespace cockpit
