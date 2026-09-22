#pragma once
// Pure particle logic: no GL, no SDL, no engine types. Unit-tested in package/tests/test_particles.cpp.
//   * presets (data-driven, with built-in defaults)
//   * a fixed-capacity pool in struct-of-arrays form: no heap allocation after construction; dead particles are swap-removed so live ones stay contiguous
//   * spawning with a seeded RNG, a per-frame spawn budget and drop-on-full
//   * per-step update (drag, age, kill) and colour / size ramps
//   * camera-relative quad building with distance and near-plane culling into preallocated arrays
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "world/star_system/star_system_api.h"

namespace fx {

using world::Vec3d;

struct Preset {
    std::string name;
    int countMin = 8, countMax = 8;
    float speedMin = 5, speedMax = 10;       // m/s relative to the emitter
    float lifeMin = 0.5f, lifeMax = 1.0f;    // seconds
    float sizeStart = 0.3f, sizeEnd = 0.0f;  // quad half-size, world units
    float colorStart[4] = {1, 1, 1, 1};      // rgba at birth
    float colorEnd[4] = {1, 1, 1, 0};        // rgba at death
    float drag = 0.0f;                       // velocity decay per second (v *= exp(-drag dt))
    bool additive = true;                    // glow presets add light; the others alpha-blend
    float spreadDeg = 180.0f;                // half angle of the cone around the emit direction (180 = all directions)
};

// The built-in presets (used when data/particles.json is missing; the shipped file has the same values).
inline std::vector<Preset> defaultPresets() {
    std::vector<Preset> v;
    auto add = [&](const char* n, int c0, int c1, float s0, float s1, float l0, float l1, float z0, float z1, const float* c, const float* e, float drag, bool add_, float spread) {
        Preset p; p.name = n; p.countMin = c0; p.countMax = c1; p.speedMin = s0; p.speedMax = s1; p.lifeMin = l0; p.lifeMax = l1; p.sizeStart = z0; p.sizeEnd = z1;
        for (int i = 0; i < 4; i++) { p.colorStart[i] = c[i]; p.colorEnd[i] = e[i]; }
        p.drag = drag; p.additive = add_; p.spreadDeg = spread; v.push_back(p);
    };
    const float exS[4] = {1.0f, 0.75f, 0.35f, 0.9f}, exE[4] = {0.9f, 0.25f, 0.05f, 0.0f};
    const float spS[4] = {1.0f, 0.95f, 0.6f, 1.0f}, spE[4] = {1.0f, 0.4f, 0.1f, 0.0f};
    const float deS[4] = {0.55f, 0.5f, 0.45f, 1.0f}, deE[4] = {0.3f, 0.28f, 0.25f, 0.0f};
    const float wfS[4] = {0.7f, 0.85f, 1.0f, 1.0f}, wfE[4] = {0.3f, 0.5f, 1.0f, 0.0f};
    const float muS[4] = {1.0f, 0.9f, 0.5f, 1.0f}, muE[4] = {1.0f, 0.5f, 0.1f, 0.0f};
    //  name          count     speed        life         size        colours   drag  additive spread
    add("exhaust",    1, 1,     10, 18,      0.35f, 0.7f, 0.5f, 0.05f, exS, exE, 0.5f, true, 20.0f);
    add("spark",      10, 18,   8, 40,       0.3f, 0.8f,  0.25f, 0.03f, spS, spE, 0.8f, true, 70.0f);
    add("debris",     4, 8,     3, 14,       0.8f, 1.6f,  0.6f, 0.4f,  deS, deE, 0.3f, false, 80.0f);
    add("warp_flash", 50, 80,   30, 120,     0.3f, 0.7f,  1.0f, 0.15f,  wfS, wfE, 1.2f, true, 180.0f);
    add("muzzle",     6, 10,    15, 50,      0.08f, 0.2f, 0.4f, 0.05f, muS, muE, 2.0f, true, 20.0f);
    return v;
}

inline const Preset* findPreset(const std::vector<Preset>& presets, const std::string& name) {
    for (auto& p : presets) if (p.name == name) return &p;
    return nullptr;
}

// Sanitise a preset read from data: no negative ranges, no swapped ranges, colours 0..1.
inline void sanitize(Preset& p) {
    p.countMin = std::max(0, p.countMin); p.countMax = std::max(p.countMin, p.countMax);
    p.speedMin = std::max(0.0f, p.speedMin); p.speedMax = std::max(p.speedMin, p.speedMax);
    p.lifeMin = std::max(0.02f, p.lifeMin); p.lifeMax = std::max(p.lifeMin, p.lifeMax);
    p.sizeStart = std::max(0.0f, p.sizeStart); p.sizeEnd = std::max(0.0f, p.sizeEnd);
    p.drag = std::max(0.0f, p.drag);
    p.spreadDeg = std::clamp(p.spreadDeg, 0.0f, 180.0f);
    for (int i = 0; i < 4; i++) { p.colorStart[i] = std::clamp(p.colorStart[i], 0.0f, 1.0f); p.colorEnd[i] = std::clamp(p.colorEnd[i], 0.0f, 1.0f); }
}

// ---- deterministic RNG (xorshift32: same numbers everywhere, no allocation) ----
struct Rng {
    uint32_t s = 2463534242u;
    explicit Rng(uint32_t seed = 1) { s = seed ? seed * 2654435761u | 1u : 2463534242u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }     // [0,1)
    float range(float a, float b) { return a + (b - a) * f(); }
    int irange(int a, int b) { return b <= a ? a : a + (int)(next() % (uint32_t)(b - a + 1)); }
};

// A random unit direction inside a cone of half angle `spreadDeg` around `axis` (axis (0,0,0) or spread >= 180 = anywhere on the sphere).
inline Vec3d randomDirection(Rng& rng, const Vec3d& axis, float spreadDeg) {
    double al = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (al < 1e-9 || spreadDeg >= 179.9f) {
        double x, y, z, l;
        do { x = rng.range(-1, 1); y = rng.range(-1, 1); z = rng.range(-1, 1); l = x * x + y * y + z * z; } while (l > 1.0 || l < 0.01);
        l = std::sqrt(l);
        return {x / l, y / l, z / l};
    }
    Vec3d a{axis.x / al, axis.y / al, axis.z / al};
    Vec3d ref = std::fabs(a.y) < 0.9 ? Vec3d{0, 1, 0} : Vec3d{1, 0, 0};
    Vec3d u{a.y * ref.z - a.z * ref.y, a.z * ref.x - a.x * ref.z, a.x * ref.y - a.y * ref.x};
    double ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
    u = {u.x / ul, u.y / ul, u.z / ul};
    Vec3d w{a.y * u.z - a.z * u.y, a.z * u.x - a.x * u.z, a.x * u.y - a.y * u.x};
    double maxAng = spreadDeg * 3.14159265358979 / 180.0;
    double cosT = 1.0 - rng.f() * (1.0 - std::cos(maxAng));               // uniform on the cap
    double sinT = std::sqrt(std::max(0.0, 1.0 - cosT * cosT)), phi = rng.f() * 6.28318530717959;
    double cx = std::cos(phi) * sinT, cy = std::sin(phi) * sinT;
    return {a.x * cosT + u.x * cx + w.x * cy, a.y * cosT + u.y * cx + w.y * cy, a.z * cosT + u.z * cx + w.z * cy};
}

// A random offset uniformly distributed over the AREA of a disc of `radius`, perpendicular to `axis` - spreads a burst's SPAWN
// POSITION across a surface (e.g. an engine nozzle's real cross-section) instead of every particle starting from one exact point,
// which is what made a narrow, fast exhaust jet look like a single rigid line/wedge rather than a plume. `axis` (0,0,0) or
// `radius` <= 0 gives {0,0,0} (no spread - every existing non-jet emitter, which passes no radius, is unaffected).
inline Vec3d randomDiscOffset(Rng& rng, const Vec3d& axis, float radius) {
    if (!(radius > 0.0f)) return {0, 0, 0};
    double al = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (al < 1e-9) return {0, 0, 0};
    Vec3d a{axis.x / al, axis.y / al, axis.z / al};
    Vec3d ref = std::fabs(a.y) < 0.9 ? Vec3d{0, 1, 0} : Vec3d{1, 0, 0};
    Vec3d u{a.y * ref.z - a.z * ref.y, a.z * ref.x - a.x * ref.z, a.x * ref.y - a.y * ref.x};
    double ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
    u = {u.x / ul, u.y / ul, u.z / ul};
    Vec3d w{a.y * u.z - a.z * u.y, a.z * u.x - a.x * u.z, a.x * u.y - a.y * u.x};
    double r = (double)radius * std::sqrt(rng.f()), phi = rng.f() * 6.28318530717959;   // sqrt(rng): uniform over the AREA, not just the radius
    double cx = std::cos(phi) * r, cy = std::sin(phi) * r;
    return {u.x * cx + w.x * cy, u.y * cx + w.y * cy, u.z * cx + w.z * cy};
}

// ---- the pool ----
struct Pool {
    // all arrays are sized once in init; `n` is the number of live particles, always the first n entries
    std::vector<double> px, py, pz;
    std::vector<float> vx, vy, vz, age, life, sizeA, sizeB, drag;
    std::vector<float> c0, c1;               // rgba start / end: 4 floats per particle
    std::vector<uint8_t> additive;
    int n = 0, capacity = 0;
    int budget = 0;                          // spawns still allowed this frame
    long dropped = 0;                        // spawns refused (pool full or over budget) since the start

    void init(int cap) {
        capacity = std::max(0, cap);
        px.assign(capacity, 0); py.assign(capacity, 0); pz.assign(capacity, 0);
        vx.assign(capacity, 0); vy.assign(capacity, 0); vz.assign(capacity, 0);
        age.assign(capacity, 0); life.assign(capacity, 1); sizeA.assign(capacity, 0); sizeB.assign(capacity, 0); drag.assign(capacity, 0);
        c0.assign((size_t)capacity * 4, 0); c1.assign((size_t)capacity * 4, 0); additive.assign(capacity, 0);
        n = 0; budget = 0; dropped = 0;
    }
    void newFrame(int spawnBudget) { budget = std::max(0, spawnBudget); }
    void clear() { n = 0; }
};

// How many particles a burst wants: the request if > 0, else a random count from the preset.
inline int burstCount(const Preset& p, int requested, Rng& rng) { return requested > 0 ? requested : rng.irange(p.countMin, p.countMax); }

struct Emit {
    Vec3d position, direction{0, 0, 0}, velocity{0, 0, 0};
    int count = 0;
    float sizeMul = 1.0f, lifeMul = 1.0f;
    float colour[3] = {-1, -1, -1};
    float sizeScale = 1.0f;                  // the fx.size_scale tunable
    float radius = 0;                        // spawn position spread (a disc perpendicular to direction, e.g. an engine nozzle's face); 0 = a single point
};

// Spawns one burst. Returns how many were actually created (limited by the free room and this frame's budget; the rest are counted as dropped).
inline int spawn(Pool& pool, const Preset& p, const Emit& e, Rng& rng) {
    int want = burstCount(p, e.count, rng);
    int room = pool.capacity - pool.n;
    int made = std::max(0, std::min({want, room, pool.budget}));
    pool.dropped += want - made;
    pool.budget -= made;
    for (int k = 0; k < made; k++) {
        int i = pool.n++;
        Vec3d off = randomDiscOffset(rng, e.direction, e.radius);
        Vec3d d = randomDirection(rng, e.direction, p.spreadDeg);
        float speed = rng.range(p.speedMin, p.speedMax);
        pool.px[i] = e.position.x + off.x; pool.py[i] = e.position.y + off.y; pool.pz[i] = e.position.z + off.z;
        pool.vx[i] = (float)(e.velocity.x + d.x * speed); pool.vy[i] = (float)(e.velocity.y + d.y * speed); pool.vz[i] = (float)(e.velocity.z + d.z * speed);
        pool.age[i] = 0;
        pool.life[i] = rng.range(p.lifeMin, p.lifeMax) * std::max(0.05f, e.lifeMul);
        float sm = std::max(0.0f, e.sizeMul) * std::max(0.0f, e.sizeScale);
        pool.sizeA[i] = p.sizeStart * sm; pool.sizeB[i] = p.sizeEnd * sm;
        pool.drag[i] = p.drag;
        pool.additive[i] = p.additive ? 1 : 0;
        for (int c = 0; c < 4; c++) { pool.c0[i * 4 + c] = p.colorStart[c]; pool.c1[i * 4 + c] = p.colorEnd[c]; }
        if (e.colour[0] >= 0.0f) for (int c = 0; c < 3; c++) { pool.c0[i * 4 + c] = e.colour[c]; pool.c1[i * 4 + c] = e.colour[c] * 0.5f; }   // colour override: fades to half brightness
    }
    return made;
}

// Advances every particle by dt: drag, motion, ageing; dead ones are swap-removed (the last live particle takes their slot).
inline void update(Pool& pool, float dt) {
    if (dt <= 0.0f) return;
    for (int i = 0; i < pool.n;) {
        pool.age[i] += dt;
        if (pool.age[i] >= pool.life[i]) {                       // dead: move the last one here and look at this slot again
            int last = --pool.n;
            if (i != last) {
                pool.px[i] = pool.px[last]; pool.py[i] = pool.py[last]; pool.pz[i] = pool.pz[last];
                pool.vx[i] = pool.vx[last]; pool.vy[i] = pool.vy[last]; pool.vz[i] = pool.vz[last];
                pool.age[i] = pool.age[last]; pool.life[i] = pool.life[last]; pool.sizeA[i] = pool.sizeA[last]; pool.sizeB[i] = pool.sizeB[last];
                pool.drag[i] = pool.drag[last]; pool.additive[i] = pool.additive[last];
                for (int c = 0; c < 4; c++) { pool.c0[i * 4 + c] = pool.c0[last * 4 + c]; pool.c1[i * 4 + c] = pool.c1[last * 4 + c]; }
            }
            continue;
        }
        float k = pool.drag[i] > 0.0f ? std::exp(-pool.drag[i] * dt) : 1.0f;
        pool.vx[i] *= k; pool.vy[i] *= k; pool.vz[i] *= k;
        pool.px[i] += (double)pool.vx[i] * dt; pool.py[i] += (double)pool.vy[i] * dt; pool.pz[i] += (double)pool.vz[i] * dt;
        i++;
    }
}

// Linear ramps over the particle's life.
inline float rampSize(const Pool& p, int i) { float t = std::clamp(p.age[i] / p.life[i], 0.0f, 1.0f); return p.sizeA[i] + (p.sizeB[i] - p.sizeA[i]) * t; }
inline void rampColour(const Pool& p, int i, float* out) {
    float t = std::clamp(p.age[i] / p.life[i], 0.0f, 1.0f);
    for (int c = 0; c < 4; c++) out[c] = p.c0[i * 4 + c] + (p.c1[i * 4 + c] - p.c0[i * 4 + c]) * t;
}

// ---- drawing ----
struct Camera { Vec3d pos; float right[3] = {1, 0, 0}, up[3] = {0, 1, 0}, fwd[3] = {0, 0, -1}; };

struct QuadLimits { float nearCull = 1.5f; float maxDist = 2500.0f; };   // nothing closer than nearCull to the camera is drawn (cockpit view: the ship's own exhaust)

// The particles worth drawing: in front of the camera, farther than nearCull, nearer than maxDist.
inline bool visible(const Camera& cam, double dx, double dy, double dz, const QuadLimits& lim) {
    double d2 = dx * dx + dy * dy + dz * dz;
    if (d2 < (double)lim.nearCull * lim.nearCull || d2 > (double)lim.maxDist * lim.maxDist) return false;
    return dx * cam.fwd[0] + dy * cam.fwd[1] + dz * cam.fwd[2] > 0.0;       // in front (a sprite behind the camera is never seen)
}

struct QuadBuffers {                         // preallocated by the caller: 4 vertices per particle
    std::vector<float> pos;                  // 3 floats per vertex
    std::vector<float> col;                  // 4 floats per vertex
    std::vector<float> uv;                   // 2 floats per vertex: the same 0..1 square for every quad, filled once
    int alphaVerts = 0, addVerts = 0;        // alpha-blended quads come first, then the additive ones
    void init(int capacity) {
        pos.assign((size_t)capacity * 4 * 3, 0); col.assign((size_t)capacity * 4 * 4, 0); uv.assign((size_t)capacity * 4 * 2, 0);
        for (int i = 0; i < capacity; i++) { float* u = &uv[(size_t)i * 8]; u[0] = 0; u[1] = 0; u[2] = 1; u[3] = 0; u[4] = 1; u[5] = 1; u[6] = 0; u[7] = 1; }
        alphaVerts = addVerts = 0;
    }
};

// Builds camera-facing quads (camera-relative floats) for every visible particle into `out`: alpha-blended ones first, then additive ones.
inline void buildQuads(const Pool& pool, const Camera& cam, const QuadLimits& lim, QuadBuffers& out) {
    out.alphaVerts = out.addVerts = 0;
    int cap = (int)(out.pos.size() / 12);
    int alphaCount = 0, addCount = 0;
    // pass 0 writes the alpha-blended particles from the start, pass 1 the additive ones right after them: two sweeps, no temporary storage
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < pool.n; i++) {
            if ((pool.additive[i] != 0) != (pass == 1)) continue;
            double dx = pool.px[i] - cam.pos.x, dy = pool.py[i] - cam.pos.y, dz = pool.pz[i] - cam.pos.z;
            if (!visible(cam, dx, dy, dz, lim)) continue;
            int slot = alphaCount + addCount;
            if (slot >= cap) break;
            float s = rampSize(pool, i), rgba[4];
            rampColour(pool, i, rgba);
            float cx = (float)dx, cy = (float)dy, cz = (float)dz;
            float rx = cam.right[0] * s, ry = cam.right[1] * s, rz = cam.right[2] * s, ux = cam.up[0] * s, uy = cam.up[1] * s, uz = cam.up[2] * s;
            float* p = &out.pos[(size_t)slot * 12];
            p[0] = cx - rx - ux; p[1] = cy - ry - uy; p[2] = cz - rz - uz;
            p[3] = cx + rx - ux; p[4] = cy + ry - uy; p[5] = cz + rz - uz;
            p[6] = cx + rx + ux; p[7] = cy + ry + uy; p[8] = cz + rz + uz;
            p[9] = cx - rx + ux; p[10] = cy - ry + uy; p[11] = cz - rz + uz;
            float* c = &out.col[(size_t)slot * 16];
            for (int v = 0; v < 4; v++) for (int k = 0; k < 4; k++) c[v * 4 + k] = rgba[k];
            (pass == 0 ? alphaCount : addCount)++;
        }
        if (pass == 0) { /* additive quads continue after the alpha ones */ }
    }
    out.alphaVerts = alphaCount * 4;
    out.addVerts = addCount * 4;
}

// A soft round dot for the particle quads: size x size RGBA8, white with alpha falling off from the centre (smooth edge, zero at the rim).
inline void softDotTexture(int size, std::vector<uint8_t>& rgba) {
    size = std::max(2, size);
    rgba.assign((size_t)size * size * 4, 255);
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
        float dx = (x + 0.5f) / size * 2.0f - 1.0f, dy = (y + 0.5f) / size * 2.0f - 1.0f;
        float r = std::sqrt(dx * dx + dy * dy), t = std::clamp(1.0f - r, 0.0f, 1.0f);
        rgba[((size_t)y * size + x) * 4 + 3] = (uint8_t)(t * t * (3.0f - 2.0f * t) * 255.0f + 0.5f);   // smoothstep falloff
    }
}

// Exhaust emission: how many particles this frame for a thrust level in 0..1 at `ratePerSecond` full-thrust rate; keeps the fractional part in `carry`.
inline int emitCount(float thrustLevel, float ratePerSecond, float dt, float& carry) {
    if (thrustLevel <= 0.0f || ratePerSecond <= 0.0f || dt <= 0.0f) { carry = 0; return 0; }
    carry += std::min(1.0f, thrustLevel) * ratePerSecond * dt;
    int n = (int)carry;
    carry -= (float)n;
    return n;
}

// Sparks/debris count from an impact speed: 3 at a scratch up to `maxCount` at 200 m/s.
inline int impactCount(float speed, int maxCount) { return std::clamp((int)(3.0f + speed * 0.2f), 3, std::max(3, maxCount)); }

} // namespace fx
