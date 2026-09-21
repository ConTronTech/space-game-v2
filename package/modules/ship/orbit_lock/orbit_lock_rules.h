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

inline Orbit makeOrbit(const Vec3d& rel, const Vec3d& relVel, const Vec3d& forward, double mu) {
    Orbit o;
    o.rel0 = rel;
    o.radius = length(rel);
    o.normal = planeNormal(rel, relVel, forward);
    o.speed = circularSpeed(mu, o.radius);
    o.omega = angularRate(o.speed, o.radius);
    return o;
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
inline Vec3d normalized(const Vec3d& a) { double l = length(a); return l > 1e-12 ? mul(a, 1.0 / l) : Vec3d{0, 0, 0}; }

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
struct Tolerances { double speedFrac = 0.12, angleDeg = 10.0; };
struct Alignment {
    Refusal reason = Refusal::None;
    double needSpeed = 0;      // circular speed at this altitude
    double relSpeed = 0;       // ship speed relative to the body
    double speedError = 0;     // relSpeed - needSpeed (positive = too fast)
    double headingDeg = 0;     // angle between the relative velocity and the orbit tangent (0 = perfectly tangential)
    double altitude = 0;
    bool aligned() const { return reason == Refusal::None; }
};

// Checked in this order: altitude band, heading (fly tangent, not radially), speed. `relVel` is the ship's velocity minus the body's.
inline Alignment checkAlignment(const Orbit& o, const Vec3d& rel, const Vec3d& relVel, double altitude, double bodyRadius,
                                double rangeFactor, double minAltitude, const Tolerances& tol) {
    Alignment a;
    a.altitude = altitude;
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

// Short, specific text for the log and the HUD ("" when aligned).
inline std::string refusalText(const Alignment& a) {
    char b[128];
    switch (a.reason) {
        case Refusal::None: return "";
        case Refusal::TooClose: return "too close to the surface";
        case Refusal::TooFar: return "too far from the body";
        case Refusal::Heading: std::snprintf(b, sizeof b, "heading %.0f degrees off the orbit (fly tangent)", a.headingDeg); return b;
        case Refusal::TooFast: std::snprintf(b, sizeof b, "speed %+.0f m/s too fast (need %.0f)", a.speedError, a.needSpeed); return b;
        case Refusal::TooSlow: std::snprintf(b, sizeof b, "speed %+.0f m/s too slow (need %.0f)", a.speedError, a.needSpeed); return b;
    }
    return "";
}

// "1.0K" above a thousand units, else whole units (the radar's style)
inline std::string altitudeText(double alt) {
    char b[32];
    if (alt >= 1000.0) std::snprintf(b, sizeof b, "%.1fK", alt / 1000.0);
    else std::snprintf(b, sizeof b, "%.0f", alt);
    return b;
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

// ---- settling onto the orbit ----
inline double smoothstep01(double x) { x = std::max(0.0, std::min(1.0, x)); return x * x * (3.0 - 2.0 * x); }

// One step of the settle, in the body's frame: the ship's velocity is blended from the one it had (relVel0) to the circular velocity at its current
// offset, weight smoothstep(t / seconds), and its distance from the body is eased to the orbit's radius the same way. `off` is the current offset
// from the body, t the time since the lock, dt the step. From t >= seconds it returns the exact circular motion. The caller re-anchors the orbit
// at the end (orbit.rel0 = rotate(off, -omega * t)) so the analytic hold continues from where the ship is: no jump, and the velocity error against
// the circular orbit shrinks monotonically to 0 (it is (1 - s) times the starting error).
inline Vec3d settleStep(const Orbit& o, const Vec3d& relVel0, const Vec3d& off, double t, double dt, double seconds) {
    double s = seconds <= 1e-6 ? 1.0 : smoothstep01((t + dt) / seconds);
    Vec3d vCirc = mul(cross(o.normal, off), o.omega);
    Vec3d v = add(mul(relVel0, 1.0 - s), mul(vCirc, s));
    Vec3d next = add(off, mul(v, dt));
    double len = length(next);
    if (len < 1e-9) return off;
    return mul(next, (len * (1.0 - s) + o.radius * s) / len);
}
// How far a relative velocity is from the circular velocity at offset `off` (for logging and tests).
inline double speedErrorVsCircular(const Orbit& o, const Vec3d& off, const Vec3d& relVel) {
    return length(sub(relVel, mul(cross(o.normal, off), o.omega)));
}

} // namespace orbit
