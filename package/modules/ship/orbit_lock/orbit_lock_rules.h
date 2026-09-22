#pragma once
// Pure orbit-lock rules: no SDL, no GL, no engine types. Unit-tested in package/tests/test_orbit_lock.cpp.
// A circular orbit is described by (start offset from the body, orbit normal, angular rate); the ship's position at any time is
// the start offset rotated about the normal by rate * time, added to the body's current position. Analytic: no integration, no drift.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "world/star_system/star_system_api.h"

namespace orbit {

using world::Vec3d;

inline Vec3d add(const Vec3d& a, const Vec3d& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3d sub(const Vec3d& a, const Vec3d& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3d mul(const Vec3d& a, double k) { return {a.x * k, a.y * k, a.z * k}; }
inline double dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3d cross(const Vec3d& a, const Vec3d& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double length(const Vec3d& a) { return std::sqrt(dot(a, a)); }
inline Vec3d normalized(const Vec3d& a) { double l = length(a); return l > 1e-12 ? mul(a, 1.0 / l) : Vec3d{0, 0, 0}; }

// ---- gravity model (a design, not real physics) ----
// mu = gravity_scale * radius^2, so the circular speed v = sqrt(mu / r) = sqrt(gravity_scale) * radius / sqrt(r) scales with the body's size:
// with the default scale 60, a 400-unit planet orbited 1,000 units from its centre goes ~98 m/s, the 1,800-unit sun at 5,400 goes ~188 m/s, a 50-unit moon at 150 goes ~31 m/s.
inline double bodyMu(double radius, double gravityScale) { return gravityScale * radius * radius; }
inline double circularSpeed(double mu, double r) { return r > 0.0 && mu > 0.0 ? std::sqrt(mu / r) : 0.0; }
inline double angularRate(double speed, double r) { return r > 0.0 ? speed / r : 0.0; }

// ---- choosing the body ----
struct Candidate { Vec3d pos; double radius = 0; };
struct Nearest { int index = -1; double altitude = 0; };      // altitude = distance from the surface (negative inside)

// The body whose SURFACE is nearest (not the nearest centre: a small moon beside a huge planet wins when you are next to it).
inline Nearest nearestBody(const std::vector<Candidate>& bodies, const Vec3d& p) {
    Nearest n;
    for (size_t i = 0; i < bodies.size(); i++) {
        double alt = length(sub(p, bodies[i].pos)) - bodies[i].radius;
        if (n.index < 0 || alt < n.altitude) { n.index = (int)i; n.altitude = alt; }
    }
    return n;
}

// Locking is allowed within `rangeFactor` radii above the surface, and never closer than minAltitude (would touch the body).
inline bool inEngageRange(double altitude, double radius, double rangeFactor, double minAltitude) {
    return altitude >= minAltitude && altitude <= rangeFactor * radius;
}

// ---- the orbit ----
// Plane normal so that rotating about it (right-hand rule) moves the offset `rel` the way the ship is already going (relVel = ship velocity
// minus body velocity). Degenerate (no motion, or straight at/away from the body): use the ship's heading, then the world up axis.
inline Vec3d planeNormal(const Vec3d& rel, const Vec3d& relVel, const Vec3d& forward) {
    auto tryNormal = [&](const Vec3d& dir, Vec3d& out) {
        Vec3d c = cross(rel, dir);
        double l = length(c), scale = length(rel) * length(dir);
        if (scale < 1e-9 || l < 1e-3 * scale) return false;   // parallel: no plane
        out = mul(c, 1.0 / l);
        return true;
    };
    Vec3d n;
    if (length(relVel) > 1.0 && tryNormal(relVel, n)) return n;
    if (tryNormal(forward, n)) return n;
    if (tryNormal({0, 1, 0}, n)) return n;
    if (tryNormal({1, 0, 0}, n)) return n;
    return {0, 1, 0};
}

// Rodrigues rotation of v about the unit axis n by `angle` radians.
inline Vec3d rotateAbout(const Vec3d& v, const Vec3d& n, double angle) {
    double c = std::cos(angle), s = std::sin(angle);
    return add(add(mul(v, c), mul(cross(n, v), s)), mul(n, dot(n, v) * (1.0 - c)));
}

struct Orbit {
    Vec3d rel0;            // offset from the body at the moment of locking
    Vec3d normal;          // unit orbit normal
    double omega = 0;      // rad/s
    double radius = 0;     // |rel0|
    double speed = 0;      // tangential speed relative to the body
};

inline Orbit makeOrbitWithNormal(const Vec3d& rel, const Vec3d& normal, double mu) {
    Orbit o;
    o.rel0 = rel;
    o.radius = length(rel);
    o.normal = normal;
    o.speed = circularSpeed(mu, o.radius);
    o.omega = angularRate(o.speed, o.radius);
    return o;
}

// ---- the guide plane, with a fallback for a radial approach (3.5d) ----
// The plane is set by the TANGENTIAL part of the relative velocity (the part across the line to the body centre). Flying (nearly) straight at or
// away from the body, that part is tiny and noisy: the plane would be set by noise and the ring/arc collapse into a line through the centre. Then
// ("fallback") the plane contains the ship's position and its nose (the part of `forward` across the radius), else its RIGHT vector (nose pointing
// at the body), else the world axes. Hysteresis so it does not flip: enter below kFallbackEnter (fraction of the speed, or kFallbackMinSpeed m/s),
// leave only above kFallbackLeave (and 1.5x the minimum speed).
constexpr double kFallbackEnter = 0.20, kFallbackLeave = 0.30, kFallbackMinSpeed = 2.0;

inline Vec3d tangentialPart(const Vec3d& rel, const Vec3d& v) {
    Vec3d u = normalized(rel);
    return sub(v, mul(u, dot(v, u)));
}

// Updates `fallback` (keep it per body between frames; start false) and returns the unit orbit normal.
inline Vec3d guidePlaneNormal(const Vec3d& rel, const Vec3d& relVel, const Vec3d& forward, bool& fallback) {
    double speed = length(relVel);
    Vec3d vt = tangentialPart(rel, relVel);
    double tl = length(vt), frac = speed > 1e-9 ? tl / speed : 0.0;
    if (fallback) { if (tl > 1.5 * kFallbackMinSpeed && frac > kFallbackLeave) fallback = false; }
    else if (tl < kFallbackMinSpeed || frac < kFallbackEnter) fallback = true;
    if (length(rel) < 1e-9) return {0, 1, 0};
    if (!fallback) return normalized(cross(rel, vt));
    Vec3d right = cross(forward, {0, 1, 0});
    if (length(right) < 1e-6) right = cross(forward, {1, 0, 0});
    const Vec3d dirs[] = {forward, right, {0, 1, 0}, {1, 0, 0}, {0, 0, 1}};
    for (const Vec3d& d : dirs) {
        Vec3d t = tangentialPart(rel, d);
        if (length(t) > 0.3 * length(d) && length(d) > 1e-9) return normalized(cross(rel, t));
    }
    return normalized(cross(rel, tangentialPart(rel, {0, 0, 1})));   // unreachable in practice: one of the axes is always across the radius
}

inline Orbit makeOrbit(const Vec3d& rel, const Vec3d& relVel, const Vec3d& forward, double mu, bool& fallback) {
    return makeOrbitWithNormal(rel, guidePlaneNormal(rel, relVel, forward, fallback), mu);
}
// Stateless (no hysteresis): what the lock does from a cold start.
inline Orbit makeOrbit(const Vec3d& rel, const Vec3d& relVel, const Vec3d& forward, double mu) {
    bool fallback = false;
    return makeOrbit(rel, relVel, forward, mu, fallback);
}

// Offset from the body after `t` seconds of orbiting.
inline Vec3d offsetAt(const Orbit& o, double t) { return rotateAbout(o.rel0, o.normal, o.omega * t); }

// World position after `t` seconds, around a body currently at `bodyPos`.
inline Vec3d positionOnOrbit(const Vec3d& bodyPos, const Orbit& o, double t) { return add(bodyPos, offsetAt(o, t)); }

// ---- releasing ----
// The lock ends when the ship dies, starts warping, or burns (thrust / strafe / lift past a small dead zone, or the brake).
inline bool shouldRelease(bool alive, bool warping, double thrust, double strafe, double lift, bool brake, bool releaseOnThrust) {
    if (!alive || warping) return true;
    if (!releaseOnThrust) return false;
    const double dead = 0.1;
    return std::fabs(thrust) > dead || std::fabs(strafe) > dead || std::fabs(lift) > dead || brake;
}

// ---- the guide: what the lock WOULD do from the ship's current state (3.5c) ----

// Tangent direction of travel at `rel` for an orbit with this normal (the way the offset moves under the right-hand rotation).
inline Vec3d orbitTangent(const Vec3d& normal, const Vec3d& rel) { return normalized(cross(normal, rel)); }

// true when the orbit runs against the world's reference rotation (normal pointing away from `refAxis`): "retrograde". Display only.
inline bool isRetrograde(const Vec3d& normal, const Vec3d& refAxis = {0, 1, 0}) { return dot(normal, refAxis) < 0; }

// The circle through `o.rel0` about `center`: segments + 1 points (the last repeats the first), point 0 at the ship, then AHEAD along the direction
// of travel (rotation about the normal by 2 pi i / segments), so the first quarter is the "arc ahead". `out` is reused (no allocation once sized).
inline void circlePoints(const Vec3d& center, const Orbit& o, int segments, std::vector<Vec3d>& out) {
    segments = std::max(3, segments);
    out.resize((size_t)segments + 1);
    const double step = 2.0 * 3.14159265358979323846 / segments;
    for (int i = 0; i <= segments; i++) out[(size_t)i] = add(center, rotateAbout(o.rel0, o.normal, step * i));
}
// How many segments of the strip make up `degrees` ahead of the ship (at least 1).
inline int arcAheadSegments(int segments, double degrees = 90.0) {
    return std::max(1, std::min(segments, (int)std::ceil(segments * degrees / 360.0 - 1e-9)));
}

// ---- alignment ----
enum class Refusal { None, TooClose, TooFar, Heading, TooFast, TooSlow };
struct Tolerances { double speedFrac = 0.25, angleDeg = 25.0; };   // forgiving defaults (3.5d); orbit.align_speed_tol / align_angle_tol
struct Alignment {
    Refusal reason = Refusal::None;
    double needSpeed = 0;      // circular speed at this altitude
    double relSpeed = 0;       // ship speed relative to the body
    double speedError = 0;     // relSpeed - needSpeed (positive = too fast)
    double headingDeg = 0;     // angle between the relative velocity and the orbit tangent (0 = perfectly tangential)
    double altitude = 0;
    double minAltitude = 0, maxAltitude = 0;   // the lock band, for the refusal text
    bool aligned() const { return reason == Refusal::None; }
};

// Checked in this order: altitude band, heading (fly tangent, not radially), speed. `relVel` is the ship's velocity minus the body's.
inline Alignment checkAlignment(const Orbit& o, const Vec3d& rel, const Vec3d& relVel, double altitude, double bodyRadius,
                                double rangeFactor, double minAltitude, const Tolerances& tol) {
    Alignment a;
    a.altitude = altitude;
    a.minAltitude = minAltitude; a.maxAltitude = rangeFactor * bodyRadius;
    a.needSpeed = o.speed;
    a.relSpeed = length(relVel);
    a.speedError = a.relSpeed - o.speed;
    if (a.relSpeed < 1e-6) a.headingDeg = 90.0;
    else {
        double c = dot(relVel, orbitTangent(o.normal, rel)) / a.relSpeed;
        a.headingDeg = std::acos(std::max(-1.0, std::min(1.0, c))) * 180.0 / 3.14159265358979323846;
    }
    if (altitude < minAltitude) a.reason = Refusal::TooClose;
    else if (altitude > rangeFactor * bodyRadius) a.reason = Refusal::TooFar;
    else if (a.headingDeg > tol.angleDeg) a.reason = Refusal::Heading;
    else if (o.speed > 0 && std::fabs(a.speedError) > tol.speedFrac * o.speed) a.reason = a.speedError > 0 ? Refusal::TooFast : Refusal::TooSlow;
    return a;
}

// "1.0K" above a thousand units, else whole units (the radar's style)
inline std::string altitudeText(double alt) {
    char b[32];
    if (alt >= 1000.0) std::snprintf(b, sizeof b, "%.1fK", alt / 1000.0);
    else std::snprintf(b, sizeof b, "%.0f", alt);
    return b;
}
// Short, specific text for the log and the HUD: what is wrong AND what to do ("" when aligned).
inline std::string refusalText(const Alignment& a) {
    char b[128];
    switch (a.reason) {
        case Refusal::None: return "";
        case Refusal::TooClose: std::snprintf(b, sizeof b, "too close to the surface: climb above %s", altitudeText(a.minAltitude).c_str()); return b;
        case Refusal::TooFar: std::snprintf(b, sizeof b, "too far: fly within %s of the surface", altitudeText(a.maxAltitude).c_str()); return b;
        case Refusal::Heading: std::snprintf(b, sizeof b, "turn %.0f deg toward the arc ahead", a.headingDeg); return b;
        case Refusal::TooFast: std::snprintf(b, sizeof b, "%.0f m/s too fast: slow to %.0f m/s", a.speedError, a.needSpeed); return b;
        case Refusal::TooSlow: std::snprintf(b, sizeof b, "%.0f m/s too slow: speed up to %.0f m/s", -a.speedError, a.needSpeed); return b;
    }
    return "";
}

// HUD block, line 1: "ORBIT PLANET 1  alt 1.0K  need 84 m/s"
inline std::string guideLine1(const std::string& body, const Alignment& a) {
    std::string up = body;
    for (auto& c : up) c = (char)std::toupper((unsigned char)c);
    char b[64];
    std::snprintf(b, sizeof b, "  alt %s  need %.0f m/s", altitudeText(a.altitude).c_str(), a.needSpeed);
    return "ORBIT " + up + b;
}
// line 2: "SPEED +12  HEADING 4 deg  [ALIGNED - press O]" or "... [<reason>]"
inline std::string guideLine2(const Alignment& a) {
    char b[192];
    std::string tail = a.aligned() ? std::string("ALIGNED - press O") : refusalText(a);
    std::snprintf(b, sizeof b, "SPEED %+.0f  HEADING %.0f deg  [%s]", a.speedError, a.headingDeg, tail.c_str());
    return b;
}

// Aligned state with hysteresis (3.5d): once aligned it stays aligned while the ship is within the tolerances x kWideTol, and only drops after
// being outside them for kHoldSeconds (values hovering at the edge no longer flicker between green and amber). The altitude band drops at once.
constexpr double kWideTol = 1.25, kHoldSeconds = 0.3;
struct AlignLatch { bool aligned = false; double outFor = 0; };
inline Tolerances widened(const Tolerances& t, double k = kWideTol) { return {t.speedFrac * k, t.angleDeg * k}; }
// `strict` checked with the tolerances, `wide` with widened() ones (same state). Returns the latched state.
inline bool updateAlignLatch(AlignLatch& l, const Alignment& strict, const Alignment& wide, double dt, double hold = kHoldSeconds) {
    if (strict.reason == Refusal::TooClose || strict.reason == Refusal::TooFar) { l = {}; return false; }
    if (strict.aligned()) { l.aligned = true; l.outFor = 0; return true; }
    if (!l.aligned) return false;
    if (wide.aligned()) { l.outFor = 0; return true; }
    l.outFor += dt;
    if (l.outFor > hold) l = {};
    return l.aligned;
}

// ---- when the guide shows (pure, 3.5d) ----
// Whenever the ship is free (alive, not warping, docking or locked) and within max(rangeFactor x radius, minRange) of the nearest SURFACE. No key press.
inline double guideRange(double bodyRadius, double rangeFactor, double minRange) { return std::max(rangeFactor * bodyRadius, minRange); }
inline bool guideVisible(bool enabled, bool locked, bool alive, bool warping, bool docking, double altitude, double bodyRadius, double rangeFactor, double minRange) {
    if (!enabled || locked || !alive || warping || docking) return false;
    return altitude >= 0.0 && altitude <= guideRange(bodyRadius, rangeFactor, minRange);
}

// ---- settling onto the orbit ----
// The settle takes longer for a bigger error (a curve, not a snap): `base` at a perfect entry up to max(base, maxSeconds) at the edge of the
// tolerances (the latch lets it lock a little beyond). The commanded acceleration of the blend peaks at 1.5 x (velocity error) / seconds.
// How far from aligned, normalized: 0 = perfect (tangent, circular speed), 1 = at or beyond the edge of a tolerance (the worse of heading and speed).
inline double alignmentCloseness(const Alignment& a, const Tolerances& tol) {
    double eh = tol.angleDeg > 0 ? a.headingDeg / tol.angleDeg : 0.0;
    double es = tol.speedFrac > 0 && a.needSpeed > 0 ? std::fabs(a.speedError) / (tol.speedFrac * a.needSpeed) : 0.0;
    return std::clamp(std::max(eh, es), 0.0, 1.0);
}

inline double settleSecondsFor(double base, const Alignment& a, const Tolerances& tol, double maxSeconds = 3.0) {
    if (base <= 0.0) return 0.0;
    return base + (std::max(base, maxSeconds) - base) * alignmentCloseness(a, tol);
}

inline double smoothstep01(double x) { x = std::max(0.0, std::min(1.0, x)); return x * x * (3.0 - 2.0 * x); }

// ---- the guide's look (w50): a colour gradient and a ring that does not crawl ----
// Amber (1.00, 0.75, 0.20) at closeness 1 to green (0.30, 1.00, 0.45) at 0, smoothstepped. Display only: locking still uses the latched boolean.
struct Rgb { float r = 0, g = 0, b = 0; };
inline Rgb guideColour(double closeness) {
    float k = (float)smoothstep01(1.0 - closeness);   // 1 = green
    return {1.00f + (0.30f - 1.00f) * k, 0.75f + (1.00f - 0.75f) * k, 0.20f + (0.45f - 0.20f) * k};
}

// Display smoothing of the plane normal: small changes ease in (exponential, time constant `tau` seconds), a big one (> snapDeg, a real turn or
// a new body) is taken at once so the ring never lags a manoeuvre. Always returns a unit vector (raw when `cur` is unusable).
constexpr double kNormalTau = 0.15, kNormalSnapDeg = 10.0;
inline Vec3d smoothNormal(const Vec3d& cur, const Vec3d& raw, double dt, double tau = kNormalTau, double snapDeg = kNormalSnapDeg) {
    Vec3d r = normalized(raw);
    if (length(r) < 0.5) return length(cur) > 0.5 ? normalized(cur) : Vec3d{0, 1, 0};
    Vec3d c = normalized(cur);
    if (length(c) < 0.5 || !(dt > 0.0) || tau <= 0.0) return r;
    double d = std::clamp(dot(c, r), -1.0, 1.0);
    if (std::acos(d) * 180.0 / 3.14159265358979323846 > snapDeg) return r;
    double k = 1.0 - std::exp(-dt / tau);
    Vec3d m = normalized(add(c, mul(sub(r, c), k)));   // nlerp: fine for the small angles that reach here
    return length(m) > 0.5 ? m : r;
}

// A fixed direction in the plane (world X projected into it, else Z): ring vertices are placed at fixed angles from it, not from the ship, so
// they stay put while the ship moves along the ring (anchoring vertex 0 at the ship made every chord and tick slide each frame: the "recast").
inline Vec3d planeReference(const Vec3d& normal) {
    Vec3d u = sub(Vec3d{1, 0, 0}, mul(normal, normal.x));
    if (length(u) < 0.3) u = sub(Vec3d{0, 0, 1}, mul(normal, normal.z));
    return normalized(u);
}
// Angle of `rel` round the normal from the reference, in [0, 2 pi), in the direction of travel.
inline double planeAngle(const Vec3d& normal, const Vec3d& ref, const Vec3d& rel) {
    double a = std::atan2(dot(cross(ref, rel), normal), dot(ref, rel));
    return a < 0 ? a + 2.0 * 3.14159265358979323846 : a;
}
// segments + 1 points of the circle of `radius` about `center` in the plane, vertex i at angle 2 pi i / segments from planeReference().
inline void stableCirclePoints(const Vec3d& center, const Vec3d& normal, double radius, int segments, std::vector<Vec3d>& out) {
    segments = std::max(3, segments);
    out.resize((size_t)segments + 1);
    Vec3d u = mul(planeReference(normal), radius);
    const double step = 2.0 * 3.14159265358979323846 / segments;
    for (int i = 0; i <= segments; i++) out[(size_t)i] = add(center, rotateAbout(u, normal, step * (i % segments)));
}
// How far AHEAD of the ship (radians, [0, 2 pi)) a vertex at angle `vertexAngle` is, the ship being at `shipAngle`.
inline double aheadAngle(double vertexAngle, double shipAngle) {
    const double tau = 2.0 * 3.14159265358979323846;
    double d = std::fmod(vertexAngle - shipAngle, tau);
    return d < 0 ? d + tau : d;
}

// Circular velocity at offset `off` for this orbit's body and plane: the circular speed at |off| (not the lock radius), along the orbit tangent.
inline double orbitMu(const Orbit& o) { return o.speed * o.speed * o.radius; }
inline Vec3d circularVelocityAt(const Orbit& o, const Vec3d& off) { return mul(orbitTangent(o.normal, off), circularSpeed(orbitMu(o), length(off))); }

// One step of the settle, in the body's frame (3.5d): the velocity is the circular velocity where the ship IS, plus the entry error
// (relVel0 minus the circular velocity at the lock point, carried round with the orbit so a radial error stays radial) faded out by
// 1 - smoothstep(t / seconds). No position snapping: the velocity is continuous, the correction acceleration peaks at 1.5 x error / seconds
// (a curve, not a snap). The ship settles on the circle at the radius where the fade ends (an inward entry ends a little lower); the caller
// then re-anchors the analytic orbit there with rebaseOrbit(): the speed is already the circular one, so there is no jump. Never below
// `minRadius` (the body's surface plus orbit.min_altitude). seconds <= 0 is the old snap onto the lock circle.
inline Vec3d settleStep(const Orbit& o, const Vec3d& relVel0, const Vec3d& off, double t, double dt, double seconds, double minRadius = 0.0) {
    if (seconds <= 1e-6) {
        Vec3d next = add(off, mul(mul(cross(o.normal, off), o.omega), dt));
        double len = length(next);
        return len < 1e-9 ? off : mul(next, o.radius / len);
    }
    double s = smoothstep01((t + dt) / seconds);
    Vec3d e0 = rotateAbout(sub(relVel0, circularVelocityAt(o, o.rel0)), o.normal, o.omega * t);
    Vec3d v = add(circularVelocityAt(o, off), mul(e0, 1.0 - s));
    Vec3d next = add(off, mul(v, dt));
    double len = length(next);
    if (len < 1e-9) return off;
    if (len < minRadius) next = mul(next, minRadius / len);
    return next;
}
// After the settle: the circular orbit through the ship's offset `off` at time t since the lock (same body, same plane).
inline Orbit rebaseOrbit(const Orbit& o, const Vec3d& off, double t) {
    Orbit r = o;
    r.radius = length(off);
    r.speed = circularSpeed(orbitMu(o), r.radius);
    r.omega = angularRate(r.speed, r.radius);
    r.rel0 = rotateAbout(off, o.normal, -r.omega * t);
    return r;
}
// How far a relative velocity is from the circular velocity at offset `off` (for logging and tests).
inline double speedErrorVsCircular(const Orbit& o, const Vec3d& off, const Vec3d& relVel) {
    return length(sub(relVel, circularVelocityAt(o, off)));
}

} // namespace orbit
