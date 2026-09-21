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

struct RadarPlot {
    float x = 0, y = 0;      // unit disc: x right, y forward
    float dist = 0;          // true 3D distance
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
    return p;
}

struct RadarContact {
    int body = -1;
    RadarPlot plot;
};

// The nearest kMaxRadarContacts offered contacts. Fixed storage: no allocation.
struct RadarContacts {
    RadarContact items[kMaxRadarContacts];
    int count = 0;
    void offer(const RadarContact& c) {
        if (count < kMaxRadarContacts) { items[count++] = c; return; }
        int far = 0;
        for (int i = 1; i < count; i++) if (items[i].plot.dist > items[far].plot.dist) far = i;
        if (c.plot.dist < items[far].plot.dist) items[far] = c;
    }
};

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

} // namespace cockpit
