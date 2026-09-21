#pragma once
// Pure star-system rules: no GL, no SDL, no engine types. Unit-tested in package/tests/test_world.cpp.
// Generation (seeded, deterministic), analytic orbits (position = f(time)), the far-body projection and a low-poly sphere.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include "world/star_system/star_system_api.h"

namespace world {

struct SystemParams {
    unsigned seed = 1234;
    int planets = 6;               // clamped to 2..10
    double sunDistance = 12000.0;  // sun centre from the world origin (where the ship spawns), so the start is clear of every body
};

struct System {
    unsigned seed = 0;
    Vec3d sun;
    std::vector<Body> bodies;      // [0] = sun, then each planet followed by its moons
};

namespace detail {
struct Rng {
    std::mt19937 g;
    explicit Rng(unsigned s) : g(s) {}
    float f() { return (float)(g() >> 8) * (1.0f / 16777216.0f); }               // [0,1), same on every platform
    float range(float a, float b) { return a + (b - a) * f(); }
    int irange(int a, int b) { return a + (int)(f() * (float)(b - a + 1)) % (b - a + 1); }   // inclusive
};
constexpr double kPi = 3.14159265358979323846;
} // namespace detail

// Old game's behaviour: sun radius 800-2000 with five star colours; planets start ~8 sun radii + 20-40k out and are spaced 30-80k apart
// (+10 planet radii), inner planets small and outer ones big; 0-3 moons each. Orbits are circular with a small inclination.
inline System generateSystem(const SystemParams& p) {
    detail::Rng rng(p.seed);
    System s;
    s.seed = p.seed;

    // where the sun sits relative to the origin: a seeded direction
    double dx, dy, dz, l;
    do { dx = rng.range(-1, 1); dy = rng.range(-0.3f, 0.3f); dz = rng.range(-1, 1); l = std::sqrt(dx * dx + dy * dy + dz * dz); } while (l < 0.2);
    s.sun = {dx / l * p.sunDistance, dy / l * p.sunDistance, dz / l * p.sunDistance};

    Body sun;
    sun.id = 0; sun.name = "Sun"; sun.kind = BodyKind::Sun; sun.position = s.sun;
    sun.radius = rng.range(800.0f, 2000.0f);
    static const float sunColors[5][3] = {{1.0f, 0.95f, 0.8f}, {0.7f, 0.85f, 1.0f}, {1.0f, 0.6f, 0.3f}, {1.0f, 0.4f, 0.2f}, {1.0f, 1.0f, 0.95f}};
    const float* sc = sunColors[rng.irange(0, 4)];
    for (int k = 0; k < 3; k++) sun.color[k] = sc[k];
    s.bodies.push_back(sun);

    int n = std::clamp(p.planets, 2, 10);
    double orbit = sun.radius * 8.0 + rng.range(20000.0f, 40000.0f);
    static const float planetColors[7][3] = {{0.3f, 0.5f, 0.8f}, {0.8f, 0.4f, 0.2f}, {0.6f, 0.55f, 0.4f}, {0.2f, 0.6f, 0.3f},
                                             {0.7f, 0.7f, 0.75f}, {0.8f, 0.6f, 0.4f}, {0.5f, 0.3f, 0.6f}};
    for (int i = 0; i < n; i++) {
        Body b;
        b.id = (int)s.bodies.size(); b.name = "Planet " + std::to_string(i + 1); b.kind = BodyKind::Planet; b.parent = 0;
        b.orbitRadius = orbit;
        double omega = rng.range(0.0001f, 0.0005f) / std::sqrt(orbit / 20000.0);          // rad/s, slower further out
        b.period = 2.0 * detail::kPi / omega;
        b.phase = rng.range(0.0f, 2.0f * (float)detail::kPi);
        b.tilt = rng.range(-10.0f, 10.0f) * detail::kPi / 180.0;
        b.radius = i < n / 2 ? rng.range(150.0f, 450.0f) : rng.range(400.0f, 1200.0f);   // inner small, outer big
        const float* c = planetColors[rng.irange(0, 6)];
        for (int k = 0; k < 3; k++) b.color[k] = c[k];
        int moons = std::min(3, rng.irange(0, 2) + (b.radius > 400.0f ? rng.irange(0, 1) : 0));
        int pid = b.id;
        float pr = b.radius;
        s.bodies.push_back(b);
        for (int m = 0; m < moons; m++) {
            Body mo;
            mo.id = (int)s.bodies.size(); mo.name = "Planet " + std::to_string(i + 1) + " Moon " + std::to_string(m + 1);
            mo.kind = BodyKind::Moon; mo.parent = pid;
            mo.orbitRadius = pr * 2.5 + rng.range(150.0f, 500.0f) + m * 400.0;
            double mw = rng.range(0.001f, 0.005f) / std::sqrt(mo.orbitRadius / 500.0);
            mo.period = 2.0 * detail::kPi / mw;
            mo.phase = rng.range(0.0f, 2.0f * (float)detail::kPi);
            mo.tilt = rng.range(-15.0f, 15.0f) * detail::kPi / 180.0;
            mo.radius = rng.range(40.0f, std::max(41.0f, pr * 0.3f));
            for (int k = 0; k < 3; k++) mo.color[k] = rng.range(0.4f, 0.7f);
            s.bodies.push_back(mo);
        }
        orbit += rng.range(30000.0f, 80000.0f) + pr * 10.0;
    }
    return s;
}

// Circular orbit around the parent, analytic in time (no integration, so no drift however long the game runs).
inline Vec3d orbitOffset(const Body& b, double t) {
    if (b.period <= 0.0) return {};
    double a = b.phase + 2.0 * detail::kPi * std::fmod(t, b.period) / b.period;
    return {b.orbitRadius * std::cos(a), b.orbitRadius * std::sin(a) * std::sin(b.tilt), b.orbitRadius * std::sin(a) * std::cos(b.tilt)};
}

// Fills every body's position for simulation time t (parents come before their moons in the list).
inline void updatePositions(System& s, double t) {
    for (auto& b : s.bodies) {
        if (b.parent < 0) { b.position = s.sun; continue; }
        Vec3d o = orbitOffset(b, t), pp = s.bodies[b.parent].position;
        b.position = {pp.x + o.x, pp.y + o.y, pp.z + o.z};
    }
}

// Physics kind of a body ("sun", "planet", "moon"): what ship damage and other consumers switch on.
inline const char* physicsKind(BodyKind k) {
    switch (k) { case BodyKind::Sun: return "sun"; case BodyKind::Planet: return "planet"; case BodyKind::Moon: return "moon"; }
    return "planet";
}

// Velocity of a body that moved from `prev` to `cur` in `dt` seconds (0 when dt <= 0): fed to the physics world so contacts
// use the closing speed relative to an orbiting body.
inline Vec3d finiteVelocity(const Vec3d& prev, const Vec3d& cur, double dt) {
    if (dt <= 0.0) return {};
    return {(cur.x - prev.x) / dt, (cur.y - prev.y) / dt, (cur.z - prev.z) / dt};
}

// ---- camera-relative drawing ----
// Bodies are stored in double. To draw: subtract the camera in double, and only then go to float. Bodies farther than clampDist
// (which must stay inside the far plane) are pulled in along the line of sight and scaled down by the same factor, so the angular size is unchanged.
struct Projected {
    float x = 0, y = 0, z = 0;     // camera-relative position to draw at
    float radius = 0;              // radius to draw with
    float dist = 0;                // true distance to the centre (for sorting and lighting)
    bool clamped = false;          // pulled in (draw as a backdrop)
};

inline Projected projectBody(const Vec3d& body, double radius, const Vec3d& cam, double clampDist) {
    double rx = body.x - cam.x, ry = body.y - cam.y, rz = body.z - cam.z;
    double d = std::sqrt(rx * rx + ry * ry + rz * rz);
    Projected p;
    p.dist = (float)d;
    double k = 1.0;
    if (d > clampDist && d > 0.0) { k = clampDist / d; p.clamped = true; }
    p.x = (float)(rx * k); p.y = (float)(ry * k); p.z = (float)(rz * k);
    p.radius = (float)(radius * k);
    return p;
}

// ---- low-poly unit sphere: (stacks+1)*(slices+1) vertices, positions double as normals ----
struct SphereMesh {
    std::vector<float> verts;                 // xyz per vertex (unit sphere, so also the normal)
    std::vector<unsigned short> indices;      // triangles
};

inline SphereMesh buildSphere(int slices, int stacks) {
    slices = std::clamp(slices, 3, 64); stacks = std::clamp(stacks, 2, 64);
    SphereMesh m;
    for (int i = 0; i <= stacks; i++) {
        double phi = detail::kPi * i / stacks;
        for (int j = 0; j <= slices; j++) {
            double th = 2.0 * detail::kPi * j / slices;
            m.verts.push_back((float)(std::sin(phi) * std::cos(th)));
            m.verts.push_back((float)std::cos(phi));
            m.verts.push_back((float)(std::sin(phi) * std::sin(th)));
        }
    }
    for (int i = 0; i < stacks; i++)
        for (int j = 0; j < slices; j++) {
            unsigned short a = (unsigned short)(i * (slices + 1) + j), b = (unsigned short)(a + slices + 1);
            m.indices.insert(m.indices.end(), {a, b, (unsigned short)(a + 1), (unsigned short)(a + 1), b, (unsigned short)(b + 1)});
        }
    return m;
}

} // namespace world
