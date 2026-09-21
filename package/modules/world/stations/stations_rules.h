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

struct ParentInfo { int bodyId = -1; double radius = 0; double moonClearance = 0; std::string name; };   // a planet a station may belong to

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
            s.surfaceRadius = p.radius * 1.02 + s.half * 3.4;   // above the tallest terrain (3% of the radius), the pillar reaches down into it
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
    Vec3d d{std::cos(s.lat) * std::sin(s.lon), std::sin(s.lat), std::cos(s.lat) * std::cos(s.lon)};
    return mul(d, s.surfaceRadius);
}

// Up axis: the surface normal for planetary stations, the orbit-plane normal (the spin axis) for orbital ones.
inline Vec3d stationUp(const Station& s) {
    using namespace sdetail;
    if (s.kind == StationKind::Orbital) return {0.0, std::cos(s.tilt), -std::sin(s.tilt)};
    return normalized({std::cos(s.lat) * std::sin(s.lon), std::sin(s.lat), std::cos(s.lat) * std::cos(s.lon)});
}

inline double spinAngle(const Station& s, double t) { return s.kind == StationKind::Orbital ? std::fmod(s.spinRate * t, 2.0 * sdetail::kPi) : 0.0; }

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
