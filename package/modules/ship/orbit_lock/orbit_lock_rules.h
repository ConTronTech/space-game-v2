#pragma once
// Pure orbit-lock rules: no SDL, no GL, no engine types. Unit-tested in package/tests/test_orbit_lock.cpp.
// A circular orbit is described by (start offset from the body, orbit normal, angular rate); the ship's position at any time is
// the start offset rotated about the normal by rate * time, added to the body's current position. Analytic: no integration, no drift.
#include <algorithm>
#include <cmath>
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

} // namespace orbit
