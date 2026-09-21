#pragma once
// Pure mining logic: no GL, no SDL, no engine types. Unit-tested in package/tests/test_mining.cpp.
//   * how much ore a destroyed asteroid holds and how it splits into chunks
//   * a fixed-capacity chunk pool (struct of arrays, oldest evicted when full)
//   * Newtonian drift, despawn (age / range), the scoop rule
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
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

// =====================================================================================================================
// Capsules (mining.mode = capsule, the default): ONE flashing capsule per destroyed asteroid, holding all its ore. A magnet pulls it to the ship,
// it is collected on contact with no speed limit, and if the ore's hold is full it stays, flashes RED and waits until it expires (or room appears).
// =====================================================================================================================
namespace gameplay {

constexpr float kCapsuleWarnSeconds = 5.0f;       // the flash speeds up during the last 5 s
constexpr float kFlashHz = 1.5f, kFlashHzFast = 6.0f;

// Flash frequency in Hz at a capsule's age: steady, then ramping up linearly over the last kCapsuleWarnSeconds.
inline float flashHz(float age, float life) {
    float left = life - age;
    if (left >= kCapsuleWarnSeconds || left <= 0.0f) return left <= 0.0f ? kFlashHzFast : kFlashHz;
    float t = 1.0f - left / kCapsuleWarnSeconds;                        // 0 .. 1 over the warning
    return kFlashHz + (kFlashHzFast - kFlashHz) * t;
}

// Flash phase in cycles (the integral of flashHz over the age): continuous, so the pulse never jumps when the speed-up starts.
inline float flashCycles(float age, float life) {
    age = std::max(0.0f, age);
    float warnStart = std::max(0.0f, life - kCapsuleWarnSeconds);
    if (age <= warnStart) return kFlashHz * age;
    float a = std::min(age, life) - warnStart, T = std::min(kCapsuleWarnSeconds, life);
    float cycles = kFlashHz * warnStart + kFlashHz * a + (kFlashHzFast - kFlashHz) * (a * a) / (2.0f * T);
    if (age > life) cycles += kFlashHzFast * (age - life);
    return cycles;
}

// The pulse 0..1 (a smooth sine, 1 = brightest / biggest).
inline float flashPulse(float age, float life) { return 0.5f + 0.5f * std::sin(6.28318530718f * flashCycles(age, life)); }

struct CapsuleLook {
    float pulse = 0;          // 0..1
    float size = 1;           // multiplier on the base size: 0.8 .. 1.4
    float white = 0;          // how white the core is: pulses with the flash
    bool red = false;         // the hold is full: a red flash
};
inline CapsuleLook capsuleLook(float age, float life, bool blocked) {
    CapsuleLook l;
    l.pulse = flashPulse(age, life);
    l.size = 0.8f + 0.6f * l.pulse;
    l.white = 0.35f + 0.65f * l.pulse;
    l.red = blocked;
    return l;
}

struct CapsulePool {
    std::vector<double> px, py, pz;
    std::vector<float> vx, vy, vz, age, life;
    std::vector<int> amount;
    std::vector<uint16_t> ore;
    std::vector<uint8_t> blocked;            // the ore's hold was full: red flash, waiting
    int n = 0, capacity = 0;
    long evicted = 0;

    void init(int cap) {
        capacity = std::max(0, cap);
        px.assign(capacity, 0); py.assign(capacity, 0); pz.assign(capacity, 0);
        vx.assign(capacity, 0); vy.assign(capacity, 0); vz.assign(capacity, 0); age.assign(capacity, 0); life.assign(capacity, 1);
        amount.assign(capacity, 0); ore.assign(capacity, 0); blocked.assign(capacity, 0);
        n = 0; evicted = 0;
    }
    // A full pool replaces the capsule closest to expiring (the least loss).
    int spawn(const Vec3d& pos, int amt, uint16_t oreIndex, float lifetime, bool isBlocked = false) {
        if (capacity <= 0 || amt <= 0) return -1;
        int slot;
        if (n < capacity) slot = n++;
        else { slot = 0; for (int i = 1; i < n; i++) if (life[i] - age[i] < life[slot] - age[slot]) slot = i; evicted++; }
        px[slot] = pos.x; py[slot] = pos.y; pz[slot] = pos.z; vx[slot] = vy[slot] = vz[slot] = 0;
        age[slot] = 0; life[slot] = std::max(0.1f, lifetime); amount[slot] = amt; ore[slot] = oreIndex; blocked[slot] = isBlocked ? 1 : 0;
        return slot;
    }
    void remove(int i) {
        int last = --n;
        if (i != last) {
            px[i] = px[last]; py[i] = py[last]; pz[i] = pz[last]; vx[i] = vx[last]; vy[i] = vy[last]; vz[i] = vz[last];
            age[i] = age[last]; life[i] = life[last]; amount[i] = amount[last]; ore[i] = ore[last]; blocked[i] = blocked[last];
        }
    }
};

// One step of a capsule's motion. Inside the magnet radius (and not blocked) it accelerates toward the ship at up to `maxSpeed` (it closes in from far away
// faster: speed grows with distance, so a slow ship still catches it); outside it drifts on with its velocity, slowly damped; a blocked capsule stops.
inline void magnetStep(CapsulePool& p, int i, const Vec3d& ship, float dt, float magnetRadius, float maxSpeed) {
    if (p.blocked[i]) { p.vx[i] = p.vy[i] = p.vz[i] = 0; return; }
    double dx = ship.x - p.px[i], dy = ship.y - p.py[i], dz = ship.z - p.pz[i], d = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (d <= magnetRadius && d > 1e-6) {
        float want = std::clamp((float)d * 2.5f + 40.0f, 40.0f, maxSpeed);            // m/s toward the ship (never slower than 40: a ship coasting past is still caught)
        float k = 1.0f - std::exp(-6.0f * dt);                                        // eases in: no jerk when the magnet catches it
        p.vx[i] += ((float)(dx / d) * want - p.vx[i]) * k; p.vy[i] += ((float)(dy / d) * want - p.vy[i]) * k; p.vz[i] += ((float)(dz / d) * want - p.vz[i]) * k;
    } else {
        float damp = std::exp(-1.5f * dt);
        p.vx[i] *= damp; p.vy[i] *= damp; p.vz[i] *= damp;
    }
    p.px[i] += (double)p.vx[i] * dt; p.py[i] += (double)p.vy[i] * dt; p.pz[i] += (double)p.vz[i] * dt;
}

// Ages the capsules and removes expired ones. dt <= 0 changes nothing.
inline void ageCapsules(CapsulePool& p, float dt) {
    if (dt <= 0.0f) return;
    for (int i = p.n - 1; i >= 0; i--) { p.age[i] += dt; if (p.age[i] >= p.life[i]) p.remove(i); }
}

// The scoop with an optional speed limit: scoopSpeed <= 0 means NO limit (capsules are collected on contact at any speed).
inline bool canScoopAny(double distance, double relativeSpeed, double scoopRadius, double scoopSpeed) {
    return distance <= scoopRadius && (scoopSpeed <= 0.0 || relativeSpeed <= scoopSpeed);
}

// What a pickup does to the capsule: `got` units went into the hold. Returns the amount left in the capsule and whether it is now blocked (red).
struct PickupOutcome { int remaining = 0; bool blocked = false; bool removeCapsule = false; };
inline PickupOutcome afterPickup(int amount, int got) {
    PickupOutcome o;
    got = std::clamp(got, 0, amount);
    o.remaining = amount - got;
    o.removeCapsule = o.remaining <= 0;
    o.blocked = o.remaining > 0;
    return o;
}

// mining.mode
enum class Mode { Capsule, Instant, Chunks };
inline bool parseMode(const std::string& s, Mode& out) {
    if (s == "capsule") out = Mode::Capsule; else if (s == "instant") out = Mode::Instant; else if (s == "chunks") out = Mode::Chunks; else return false;
    return true;
}

// What happens to a destroyed rock's ore: capsule mode leaves a capsule (or, when the ship is farther than `range`, goes straight to the hold like instant mode);
// instant adds it at once and only the part that did not fit becomes a capsule; chunks scatters chunks.
inline bool goesStraightToHold(Mode m, double shipDistance, double range) { return m == Mode::Instant || (m == Mode::Capsule && shipDistance > range); }

} // namespace gameplay
