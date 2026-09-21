#pragma once
// Pure mining logic: no GL, no SDL, no engine types. Unit-tested in package/tests/test_mining.cpp.
//   * how much ore a destroyed asteroid holds and how it splits into chunks
//   * a fixed-capacity chunk pool (struct of arrays, oldest evicted when full)
//   * Newtonian drift, despawn (age / range), the scoop rule
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "world/star_system/star_system_api.h"

namespace gameplay {

using world::Vec3d;

// Total ore in units: yield_scale * radius^2 (scale 0.5: radius 9 -> 41, radius 3 -> 5), at least 1.
inline int totalYield(float radius, float yieldScale) { return std::max(1, (int)std::lround((double)yieldScale * radius * radius)); }

// How many chunks: about 1 + sqrt(total), times the tunable, between 1 and min(total, 16) (a chunk holds at least 1 unit).
inline int chunkCount(int total, float countScale) {
    if (total <= 0) return 0;
    int n = (int)std::lround((1.0 + std::sqrt((double)total)) * std::max(0.0f, countScale));
    return std::clamp(n, 1, std::min(total, 16));
}

// Splits `total` into `n` parts that sum to exactly `total` (parts differ by at most 1; larger ones first).
inline void splitAmount(int total, int n, std::vector<int>& out) {
    out.clear();
    if (total <= 0 || n <= 0) return;
    n = std::min(n, total);
    for (int i = 0; i < n; i++) out.push_back(total / n + (i < total % n ? 1 : 0));
}

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 1) : s(seed ? seed * 2654435761u | 1u : 2463534242u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * f(); }
};

// A random outward velocity for a chunk: a random direction times a speed in [minSpeed, maxSpeed], plus the asteroid's own velocity (0: they are static).
inline Vec3d chunkVelocity(Rng& rng, float minSpeed, float maxSpeed, const Vec3d& asteroidVel = {}) {
    double x, y, z, l;
    do { x = rng.range(-1, 1); y = rng.range(-1, 1); z = rng.range(-1, 1); l = x * x + y * y + z * z; } while (l > 1.0 || l < 0.01);
    l = std::sqrt(l);
    float sp = rng.range(minSpeed, maxSpeed);
    return {asteroidVel.x + x / l * sp, asteroidVel.y + y / l * sp, asteroidVel.z + z / l * sp};
}

// The scoop: close enough AND slow enough relative to the chunk. A fast fly-through collects nothing.
inline bool canScoop(double distance, double relativeSpeed, double scoopRadius, double scoopSpeed) { return distance <= scoopRadius && relativeSpeed <= scoopSpeed; }

// ---- the chunk pool ----
struct ChunkPool {
    std::vector<double> px, py, pz;
    std::vector<float> vx, vy, vz, age;
    std::vector<int> amount;
    std::vector<uint16_t> ore;               // index into the module's ore table
    std::vector<uint8_t> blocked;            // the hold refused this chunk: retry only after the ship has left the scoop radius and come back
    int n = 0, capacity = 0;
    long evicted = 0;

    void init(int cap) {
        capacity = std::max(0, cap);
        px.assign(capacity, 0); py.assign(capacity, 0); pz.assign(capacity, 0);
        vx.assign(capacity, 0); vy.assign(capacity, 0); vz.assign(capacity, 0); age.assign(capacity, 0);
        amount.assign(capacity, 0); ore.assign(capacity, 0); blocked.assign(capacity, 0);
        n = 0; evicted = 0;
    }
    // Adds a chunk; when full the OLDEST chunk is replaced. Returns the slot, -1 if capacity is 0.
    int spawn(const Vec3d& pos, const Vec3d& vel, int amt, uint16_t oreIndex) {
        if (capacity <= 0 || amt <= 0) return -1;
        int slot;
        if (n < capacity) slot = n++;
        else {
            slot = 0;
            for (int i = 1; i < n; i++) if (age[i] > age[slot]) slot = i;
            evicted++;
        }
        px[slot] = pos.x; py[slot] = pos.y; pz[slot] = pos.z; vx[slot] = (float)vel.x; vy[slot] = (float)vel.y; vz[slot] = (float)vel.z;
        age[slot] = 0; amount[slot] = amt; ore[slot] = oreIndex; blocked[slot] = 0;
        return slot;
    }
    void remove(int i) {                      // swap-remove: the last live chunk takes slot i
        int last = --n;
        if (i != last) {
            px[i] = px[last]; py[i] = py[last]; pz[i] = pz[last]; vx[i] = vx[last]; vy[i] = vy[last]; vz[i] = vz[last];
            age[i] = age[last]; amount[i] = amount[last]; ore[i] = ore[last]; blocked[i] = blocked[last];
        }
    }
};

// Newtonian: no drag. Ages every chunk and drops the ones older than `lifetime` or farther than `range` from `ref` (the ship). dt <= 0 changes nothing.
inline void advanceChunks(ChunkPool& p, float dt, float lifetime, float range, const Vec3d& ref) {
    if (dt <= 0.0f) return;
    for (int i = p.n - 1; i >= 0; i--) {
        p.age[i] += dt;
        p.px[i] += (double)p.vx[i] * dt; p.py[i] += (double)p.vy[i] * dt; p.pz[i] += (double)p.vz[i] * dt;
        double dx = p.px[i] - ref.x, dy = p.py[i] - ref.y, dz = p.pz[i] - ref.z;
        if (p.age[i] >= lifetime || dx * dx + dy * dy + dz * dz > (double)range * range) p.remove(i);
    }
}

} // namespace gameplay
