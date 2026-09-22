#pragma once
// Pure ship-gravity rules: no SDL, no GL, no engine types. Unit-tested in package/tests/test_gravity.cpp. See docs/GRAVITY.md.
// Patched conics (single-body sphere of influence): the ship feels ONE body at a time, the innermost sphere of influence it is inside
// (moon > planet > the sun fallback, out to gravity.sun_range; beyond that, no gravity at all). The bodies themselves stay on their rails.
#include <algorithm>
#include <cmath>
#include <vector>
#include "ship/orbit_lock/orbit_lock_rules.h"   // orbit::bodyMu: the SAME mu the orbit lock/guide uses (one gravity constant, orbit.gravity_scale)
#include "world/star_system/star_system_api.h"

namespace gravity {

using world::Vec3d;

// mu = orbit.gravity_scale * radius^2 (orbit::bodyMu). Note the surface gravity is therefore gravity_scale (60 m/s^2 by default) for every body.
inline double bodyMu(double radius, double gravityScale) { return orbit::bodyMu(radius, gravityScale); }

// Laplace sphere of influence: r_soi = a * (mu_body / mu_parent)^(2/5), a = the body's orbit radius around its parent.
inline double soiRadius(double orbitRadius, double muBody, double muParent) {
    if (orbitRadius <= 0.0 || muBody <= 0.0 || muParent <= 0.0) return 0.0;
    return orbitRadius * std::pow(muBody / muParent, 0.4);
}

struct Source {
    Vec3d pos;
    double radius = 0, mu = 0, soi = 0;   // soi: the sun's is gravity.sun_range
    int kind = 1;                         // 0 = sun, 1 = planet, 2 = moon (the innermost wins)
};

// mu and SOI for every body from what IStarSystem already exposes (index = body id). Reuses `out` (no allocation once sized).
// sunRangeFactor: the sun's range = factor x the outermost planet's orbit radius.
inline void buildSources(const std::vector<world::Body>& bodies, double gravityScale, double sunRangeFactor, std::vector<Source>& out) {
    out.resize(bodies.size());
    double outer = 0;
    for (size_t i = 0; i < bodies.size(); i++) {
        const auto& b = bodies[i];
        Source& s = out[i];
        s.pos = b.position; s.radius = b.radius; s.mu = bodyMu(b.radius, gravityScale);
        s.kind = b.kind == world::BodyKind::Sun ? 0 : b.kind == world::BodyKind::Moon ? 2 : 1;
        if (s.kind == 1) outer = std::max(outer, b.orbitRadius);
    }
    for (size_t i = 0; i < bodies.size(); i++) {
        const auto& b = bodies[i];
        if (out[i].kind == 0) { out[i].soi = sunRangeFactor * outer; continue; }
        double muParent = (b.parent >= 0 && b.parent < (int)bodies.size()) ? out[(size_t)b.parent].mu : 0.0;
        out[i].soi = soiRadius(b.orbitRadius, out[i].mu, muParent);
    }
}

// The dominant body: the innermost SOI containing the ship (moon > planet > sun; among equals the nearest), or -1 (deep space: no gravity).
// Hysteresis: the current body's SOI counts as `hysteresis` x bigger, so it is kept until the ship is clearly out (no flicker at the edge).
inline int dominantBody(const std::vector<Source>& src, const Vec3d& ship, int current, double hysteresis) {
    int best = -1;
    double bestDist = 0;
    for (size_t i = 0; i < src.size(); i++) {
        const Source& s = src[i];
        if (s.soi <= 0.0) continue;
        double d = orbit::length(orbit::sub(ship, s.pos));
        double limit = s.soi * ((int)i == current ? std::max(1.0, hysteresis) : 1.0);
        if (d >= limit) continue;
        if (best < 0 || s.kind > src[(size_t)best].kind || (s.kind == src[(size_t)best].kind && d < bestDist)) { best = (int)i; bestDist = d; }
    }
    return best;
}

// a = -mu / max(r, minRadius)^2 * unit(ship - body), |a| clamped to maxAccel (0 = no clamp). minRadius <= 0 means the body's own radius.
// At the exact centre there is no direction: zero (never NaN).
inline Vec3d acceleration(const Source& s, const Vec3d& ship, double minRadius, double maxAccel) {
    Vec3d d = orbit::sub(ship, s.pos);
    double r = orbit::length(d);
    if (r < 1e-9 || s.mu <= 0.0) return {0, 0, 0};
    double rMin = minRadius > 0.0 ? minRadius : s.radius;
    double re = std::max(r, rMin);
    double a = s.mu / (re * re);
    if (maxAccel > 0.0) a = std::min(a, maxAccel);
    return orbit::mul(d, -a / r);
}

// One semi-implicit (symplectic) Euler kick: v_new = v + a * dt. The ship then drifts with position += v_new * dt itself.
inline Vec3d kick(const Vec3d& v, const Vec3d& a, double dt) { return orbit::add(v, orbit::mul(a, dt)); }

// Gravity is skipped entirely while any of these hold (the orbit lock already holds an exact orbit; docking places the ship; warp has its own motion).
inline bool shouldApply(bool enabled, bool alive, bool held, bool warping, bool orbitLocked, bool paused) {
    return enabled && alive && !held && !warping && !orbitLocked && !paused;
}

// Dock-approach assist (task 3.5f): gravity's magnitude fades near the station the ship could dock at (IDocking::nearestDockable), so the final
// approach to a planetary pad is not a fight against full surface gravity. `distance` is from the station centre (the dock-zone metric),
// `zone` is the station's dock-zone radius, `range` the distance where the fade begins. 1 at/beyond range, `minFactor` at/inside the zone,
// smoothstep in between (same easing as the orbit guide). Degenerate inputs (NaN, range <= zone, ...) fall back to 1 = no change.
inline double dockAssistFactor(double distance, double zone, double range, double minFactor) {
    if (!(distance == distance) || !(range > 0.0) || !(minFactor == minFactor)) return 1.0;
    double lo = std::clamp(minFactor, 0.0, 1.0);
    zone = std::max(0.0, zone == zone ? zone : 0.0);
    if (range <= zone) return 1.0;
    if (distance >= range) return 1.0;
    if (distance <= zone) return lo;
    return lo + (1.0 - lo) * orbit::smoothstep01((distance - zone) / (range - zone));
}
// The factor actually applied: exactly 1 unless a station was reported (`found`) and the assist is on.
inline double dockAssist(bool found, bool enabled, double distance, double zone, double range, double minFactor) {
    return found && enabled ? dockAssistFactor(distance, zone, range, minFactor) : 1.0;
}

// Specific orbital energy and eccentricity relative to the body (debug sanity numbers: a circular orbit has e ~ 0, energy -mu/2r).
struct OrbitInfo { double energy = 0, ecc = 0; };
inline OrbitInfo orbitInfo(const Vec3d& rel, const Vec3d& relVel, double mu) {
    OrbitInfo o;
    double r = orbit::length(rel);
    if (r < 1e-9 || mu <= 0.0) return o;
    double v2 = orbit::dot(relVel, relVel);
    o.energy = 0.5 * v2 - mu / r;
    // e = ((v^2 - mu/r) r - (r.v) v) / mu
    Vec3d e = orbit::mul(orbit::sub(orbit::mul(rel, v2 - mu / r), orbit::mul(relVel, orbit::dot(rel, relVel))), 1.0 / mu);
    o.ecc = orbit::length(e);
    return o;
}

// Forecast: `n` steps of the same symplectic scheme around a fixed body (relative frame), into out[0..n] (out must hold n + 1 points).
// Stops early inside the body's surface; returns the number of points written.
inline int forecast(const Source& s, Vec3d pos, Vec3d vel, double dt, int n, double minRadius, double maxAccel, Vec3d* out) {
    int k = 0;
    out[k++] = pos;
    for (int i = 0; i < n; i++) {
        vel = kick(vel, acceleration(s, pos, minRadius, maxAccel), dt);
        pos = orbit::add(pos, orbit::mul(vel, dt));
        out[k++] = pos;
        if (orbit::length(orbit::sub(pos, s.pos)) < s.radius) break;
    }
    return k;
}

} // namespace gravity
