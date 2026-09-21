#pragma once
// Pure station rules: no GL, no SDL, no engine types. Unit-tested in package/tests/test_stations.cpp.
//   * seeded generation of 1-3 stations (orbital or planetary) around planets
//   * analytic positions: an orbital station circles its planet like a moon, a planetary one sits at a fixed latitude/longitude (planets do not rotate in V2)
//   * the docking checks
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include "world/star_system/planet_mesh.h"   // surfaceRadiusFactor: the planet terrain the surface stations sit on
#include "world/stations/stations_api.h"

namespace world {

struct Station {
    std::string name;
    StationKind kind = StationKind::Orbital;
    int parent = -1;                 // planet body id
    double parentRadius = 0;
    float scale = 2.0f;              // 2, or 3 around planets bigger than 600
    float dockRadius = 120.0f;       // scale * 60
    float half = 20.0f;              // model half extent: scale * 10 (a 40-60 unit platform)
    // orbital
    double orbitRadius = 0, phase = 0, omega = 0, tilt = 0, spinRate = 0;
    // planetary
    double lat = 0, lon = 0, surfaceRadius = 0;
};

// A planet a station may belong to. `terrain` is its mesh description (seed, relief, ocean): terrain.terrainHeight 0 = a smooth sphere.
struct ParentInfo { int bodyId = -1; double radius = 0; double moonClearance = 0; std::string name; PlanetParams terrain{0, 0.0f}; };

namespace sdetail {
constexpr double kPi = 3.14159265358979323846;
struct Rng {
    std::mt19937 g;
    explicit Rng(unsigned s) : g(s) {}
    float f() { return (float)(g() >> 8) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * f(); }
    int irange(int a, int b) { return a + (int)(f() * (float)(b - a + 1)) % (b - a + 1); }
};
inline Vec3d add(const Vec3d& a, const Vec3d& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3d sub(const Vec3d& a, const Vec3d& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3d mul(const Vec3d& a, double k) { return {a.x * k, a.y * k, a.z * k}; }
inline double dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double length(const Vec3d& a) { return std::sqrt(dot(a, a)); }
inline Vec3d normalized(const Vec3d& a) { double l = length(a); return l > 0 ? mul(a, 1.0 / l) : Vec3d{0, 1, 0}; }
} // namespace sdetail

// The asteroid cluster around a planet (world/asteroids) spans 4.5 R + 500 .. + max(400, 3 R): orbital stations stay outside it (the formula is
// replicated here, not read from IAsteroids, so stations do not depend on that module).
inline double clusterOuterRadius(double parentRadius) { return parentRadius * 4.5 + 500.0 + std::max(400.0, parentRadius * 3.0); }
inline double safeOrbitMin(double parentRadius, double moonClearance) {
    double clusterInner = std::max(parentRadius * 4.5 + 500.0, moonClearance + parentRadius * 1.2);
    double clusterOuter = std::max(clusterOuterRadius(parentRadius), clusterInner + parentRadius * 3.0);
    return clusterOuter + parentRadius * 0.5;
}

constexpr double kStationEmbed = 0.1;   // the cube's bottom sinks this fraction of its height into the ground, so no gap shows on slopes

inline Vec3d latLonDir(double lat, double lon) { return {std::cos(lat) * std::sin(lon), std::sin(lat), std::cos(lat) * std::cos(lon)}; }
inline Vec3d referenceForward(const Vec3d& up);

// Local terrain radius (units, from the planet centre) along the unit direction d: the same function the planet mesh is built from.
inline double localSurfaceRadius(const ParentInfo& p, const Vec3d& d) {
    return p.radius * (double)surfaceRadiusFactor(p.terrain, (float)d.x, (float)d.y, (float)d.z);
}
// Distance from the planet centre to the cube centre of a surface station: its bottom face (up = the radial direction, so the pad stays level
// for docking) rests on the LOWEST terrain under its footprint (centre, 4 bottom corners, 4 edge midpoints), sunk kStationEmbed of its height.
inline double groundedRadius(const ParentInfo& p, double lat, double lon, float half) {
    using namespace sdetail;
    Vec3d up = latLonDir(lat, lon), f = referenceForward(up);
    Vec3d r{up.y * f.z - up.z * f.y, up.z * f.x - up.x * f.z, up.x * f.y - up.y * f.x};
    double ground = 1e300;
    for (int i = -1; i <= 1; i++) for (int k = -1; k <= 1; k++) {
        Vec3d q = add(mul(up, p.radius), add(mul(r, i * (double)half), mul(f, k * (double)half)));
        Vec3d d = normalized(q);
        ground = std::min(ground, localSurfaceRadius(p, d) * dot(d, up));   // height of that ground point along up
    }
    return ground + (double)half * (1.0 - 2.0 * kStationEmbed);
}

// Old game's behaviour: 1-3 stations (tunable), 50/50 orbital vs planetary, on random planets; orbital ones outside the moons and the cluster on a tilted
// orbit (+-10 degrees); planetary ones at a latitude within +-60 degrees. Same seed = same stations.
inline std::vector<Station> generateStations(unsigned seed, int count, const std::vector<ParentInfo>& parents) {
    std::vector<Station> out;
    if (parents.empty()) return out;
    sdetail::Rng rng(seed + 9999U);
    count = std::clamp(count, 0, 3);
    for (int i = 0; i < count; i++) {
        const ParentInfo& p = parents[rng.irange(0, (int)parents.size() - 1)];
        Station s;
        s.parent = p.bodyId; s.parentRadius = p.radius;
        s.kind = rng.irange(0, 1) == 0 ? StationKind::Orbital : StationKind::Planetary;
        s.scale = p.radius > 600.0 ? 3.0f : 2.0f;
        s.dockRadius = s.scale * 60.0f;
        s.half = s.scale * 10.0f;
        s.name = "Station " + std::to_string(i + 1) + " (" + p.name + ", " + (s.kind == StationKind::Orbital ? "orbital" : "surface") + ")";
        if (s.kind == StationKind::Orbital) {
            s.orbitRadius = safeOrbitMin(p.radius, p.moonClearance) + rng.range(100.0f, 500.0f);
            s.omega = rng.range(0.002f, 0.008f) / std::sqrt(s.orbitRadius / 500.0);
            s.phase = rng.range(0.0f, 2.0f * (float)sdetail::kPi);
            s.tilt = rng.range(-10.0f, 10.0f) * sdetail::kPi / 180.0;
            s.spinRate = rng.range(0.01f, 0.05f);
        } else {
            s.lat = rng.range(-60.0f, 60.0f) * sdetail::kPi / 180.0;
            s.lon = rng.range(0.0f, 2.0f * (float)sdetail::kPi);
            s.surfaceRadius = groundedRadius(p, s.lat, s.lon, s.half);   // the cube sits on the local terrain
        }
        out.push_back(s);
    }
    return out;
}

// Offset of the station from its parent planet's centre at simulation time t.
inline Vec3d stationOffset(const Station& s, double t) {
    using namespace sdetail;
    if (s.kind == StationKind::Orbital) {
        double a = s.phase + s.omega * t;      // circular, analytic (a is used through sin/cos only, so no wrap needed)
        return {s.orbitRadius * std::cos(a), s.orbitRadius * std::sin(a) * std::sin(s.tilt), s.orbitRadius * std::sin(a) * std::cos(s.tilt)};
    }
    return mul(latLonDir(s.lat, s.lon), s.surfaceRadius);
}

// Up axis: the surface normal for planetary stations, the orbit-plane normal (the spin axis) for orbital ones.
inline Vec3d stationUp(const Station& s) {
    using namespace sdetail;
    if (s.kind == StationKind::Orbital) return {0.0, std::cos(s.tilt), -std::sin(s.tilt)};
    return normalized(latLonDir(s.lat, s.lon));
}

inline double spinAngle(const Station& s, double t) { return s.kind == StationKind::Orbital ? std::fmod(s.spinRate * t, 2.0 * sdetail::kPi) : 0.0; }

// ---- station frame and landing pad ----
namespace sdetail {
inline Vec3d cross(const Vec3d& a, const Vec3d& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
// Rodrigues: v rotated about the unit axis by angle (right-hand rule).
inline Vec3d rotateAbout(const Vec3d& v, const Vec3d& axis, double angle) {
    double c = std::cos(angle), s = std::sin(angle);
    return add(add(mul(v, c), mul(cross(axis, v), s)), mul(axis, dot(axis, v) * (1.0 - c)));
}
} // namespace sdetail

// The station's reference axes when it has not spun: forward is a fixed function of `up` (the same basis the model is drawn with).
// (right, up, forward) is right-handed: cross(right, up) = forward, cross(up, forward) = right.
inline Vec3d referenceForward(const Vec3d& up) {
    using namespace sdetail;
    Vec3d ref = std::fabs(up.z) < 0.9 ? Vec3d{0, 0, 1} : Vec3d{1, 0, 0};
    Vec3d r = normalized(cross(up, ref));
    return normalized(cross(r, up));
}
// forward at spin angle `angle` (orbital stations spin about up; planetary ones pass 0).
inline Vec3d spunForward(const Vec3d& up, double angle) { return sdetail::rotateAbout(referenceForward(up), up, angle); }

// Everything the docking code needs to know about a station at one instant.
struct StationPose {
    Vec3d pos, vel, up, forward;
    double spinRate = 0;
    double padTop = 23.2;
};
inline Vec3d poseRight(const StationPose& p) { return sdetail::normalized(sdetail::cross(p.up, p.forward)); }

// The ship's heading on the pad, kept in the STATION frame: components along the pad's right and forward axes (unit length).
struct PadHeading { double right = 0, forward = 1; };

// Capture at dock start: the ship's forward projected onto the pad plane (its component along `up` removed). A heading that points along up
// (no usable projection) falls back to the station's forward.
inline PadHeading captureHeading(const StationPose& p, const Vec3d& shipForward) {
    using namespace sdetail;
    Vec3d flat = sub(shipForward, mul(p.up, dot(shipForward, p.up)));
    double l = length(flat);
    if (l < 1e-6) return {0, 1};
    flat = mul(flat, 1.0 / l);
    return {dot(flat, poseRight(p)), dot(flat, normalized(p.forward))};
}

struct PadPose { Vec3d pos, forward, up; };

// The pose of a ship resting on the pad, at the station's CURRENT pose: pad centre + up * (pad top + restHeight), belly toward the pad
// (ship up = station up), heading fixed in the station frame. This is the whole trick: no integration, so nothing drifts.
inline PadPose padPose(const StationPose& p, const PadHeading& h, double restHeight) {
    using namespace sdetail;
    PadPose out;
    out.up = normalized(p.up);
    out.pos = add(p.pos, mul(out.up, p.padTop + restHeight));
    Vec3d f = add(mul(poseRight(p), h.right), mul(normalized(p.forward), h.forward));
    f = sub(f, mul(out.up, dot(f, out.up)));               // stay exactly in the pad plane
    out.forward = normalized(f);
    return out;
}

// True velocity of a point fixed to the (moving, spinning) station: v + spinRate * up x r.
inline Vec3d padPointVelocity(const StationPose& p, const Vec3d& worldPoint) {
    using namespace sdetail;
    return add(p.vel, mul(cross(p.up, sub(worldPoint, p.pos)), p.spinRate));
}

// ---- the approach ----
inline double smoothstep(double t) { t = std::clamp(t, 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }
// 0..1 progress of an approach that takes `seconds` (<= 0: instant).
inline double approachProgress(double elapsed, double seconds) { return seconds <= 0 ? 1.0 : std::clamp(elapsed / seconds, 0.0, 1.0); }

// Normalised linear blend of two orientations, returned orthonormal (forward is made perpendicular to up). s = 0 gives from, s = 1 gives to.
inline void blendOrientation(const Vec3d& f0, const Vec3d& u0, const Vec3d& f1, const Vec3d& u1, double s, Vec3d& fOut, Vec3d& uOut) {
    using namespace sdetail;
    Vec3d u = normalized(add(mul(u0, 1.0 - s), mul(u1, s)));
    Vec3d f = add(mul(f0, 1.0 - s), mul(f1, s));
    f = sub(f, mul(u, dot(f, u)));
    if (length(f) < 1e-6) f = sub(f1, mul(u, dot(f1, u)));      // opposite headings: any perpendicular one is better than none
    fOut = normalized(f);
    uOut = u;
}

// ---- docking ----
enum class DockCheck { Ok, NoStation, Dead, Warping, OrbitLocked, AlreadyDocked, TooFar, TooFast };

inline DockCheck canDock(bool alive, bool warping, bool orbitLocked, bool docked, bool hasStation, double distance, double dockRadius, double relSpeed, double maxSpeed) {
    if (docked) return DockCheck::AlreadyDocked;
    if (!alive) return DockCheck::Dead;
    if (warping) return DockCheck::Warping;
    if (orbitLocked) return DockCheck::OrbitLocked;
    if (!hasStation) return DockCheck::NoStation;
    if (distance > dockRadius) return DockCheck::TooFar;
    if (relSpeed > maxSpeed) return DockCheck::TooFast;
    return DockCheck::Ok;
}

inline const char* dockReason(DockCheck c) {
    switch (c) {
        case DockCheck::Ok: return "ok";
        case DockCheck::NoStation: return "no station";
        case DockCheck::Dead: return "ship destroyed";
        case DockCheck::Warping: return "warp drive engaged";
        case DockCheck::OrbitLocked: return "orbit lock engaged";
        case DockCheck::AlreadyDocked: return "already docked";
        case DockCheck::TooFar: return "too far from the station";
        case DockCheck::TooFast: return "too fast";
    }
    return "?";
}

// The velocity that makes a ship integrating position += velocity * dt land on `target` (steering, exactly like the orbit lock).
inline Vec3d steerVelocity(const Vec3d& target, const Vec3d& pos, double dt) { return dt > 0 ? sdetail::mul(sdetail::sub(target, pos), 1.0 / dt) : Vec3d{}; }

// Leaving the dock: the station's velocity plus a small push straight out from the station.
inline Vec3d undockVelocity(const Vec3d& stationVel, const Vec3d& shipPos, const Vec3d& stationPos, double push) {
    using namespace sdetail;
    return add(stationVel, mul(normalized(sub(shipPos, stationPos)), push));
}

// Thrust, strafe or lift past a small dead zone undocks.
inline bool shouldUndock(double thrust, double strafe, double lift) {
    const double dead = 0.1;
    return std::fabs(thrust) > dead || std::fabs(strafe) > dead || std::fabs(lift) > dead;
}

} // namespace world
