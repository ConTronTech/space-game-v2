#pragma once
// Pure weapon logic: no GL, no SDL, no engine types. Unit-tested in package/tests/test_weapons.cpp.
//   * weapon definitions (data-driven; built-in defaults)
//   * heat / rate-of-fire / lockout state machine
//   * a fixed-capacity bolt pool (struct of arrays, no allocation after init)
//   * swept segment-vs-sphere and ray-vs-sphere tests
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "world/star_system/star_system_api.h"

namespace combat {

using world::Vec3d;

enum class Kind { Projectile, Beam };

struct WeaponDef {
    std::string name = "blaster";
    Kind kind = Kind::Projectile;
    float damage = 10.0f;               // per bolt
    float speed = 600.0f;               // bolt speed relative to the shooter, m/s
    float lifetime = 2.5f;              // bolt life, seconds
    float range = 500.0f;               // beam length, units
    float rateOfFire = 5.0f;            // shots per second
    float heatPerShot = 0.1f;           // heat 0..1 added per shot
    float heatPerSecond = 0.0f;         // heat added per second while a beam fires
    float cooldownRate = 0.25f;         // heat lost per second
    float lockoutSeconds = 2.0f;        // cannot fire this long after overheating
    float recoil = 0.5f;                // m/s of ship speed per shot (nominal ship mass)
    float spread = 0.6f;                // half angle of the random cone, degrees
    float muzzle[3] = {0.0f, -0.3f, 3.6f};   // offset in the ship frame: x right, y up, z forward
    float colour[3] = {0.4f, 0.9f, 1.0f};
    float beamDps = 0.0f;               // damage per second on asteroids
};

inline std::vector<WeaponDef> defaultWeapons() {
    std::vector<WeaponDef> v;
    WeaponDef b;                                    // the blaster
    v.push_back(b);
    WeaponDef m;                                    // the mining beam
    m.name = "mining beam"; m.kind = Kind::Beam; m.damage = 0; m.speed = 0; m.lifetime = 0; m.range = 500; m.rateOfFire = 0;
    m.heatPerShot = 0; m.heatPerSecond = 0.15f; m.cooldownRate = 0.3f; m.lockoutSeconds = 2.5f; m.recoil = 0; m.spread = 0;
    m.colour[0] = 1.0f; m.colour[1] = 0.6f; m.colour[2] = 0.2f; m.beamDps = 30.0f;
    v.push_back(m);
    return v;
}

// Keeps a definition in sane bounds (data files can be wrong).
inline void sanitize(WeaponDef& w) {
    w.damage = std::max(0.0f, w.damage); w.speed = std::max(0.0f, w.speed); w.lifetime = std::max(0.05f, w.lifetime);
    w.range = std::max(1.0f, w.range); w.rateOfFire = std::clamp(w.rateOfFire, 0.0f, 60.0f);
    w.heatPerShot = std::max(0.0f, w.heatPerShot); w.heatPerSecond = std::max(0.0f, w.heatPerSecond);
    w.cooldownRate = std::max(0.0f, w.cooldownRate); w.lockoutSeconds = std::max(0.0f, w.lockoutSeconds);
    w.recoil = std::max(0.0f, w.recoil); w.spread = std::clamp(w.spread, 0.0f, 45.0f); w.beamDps = std::max(0.0f, w.beamDps);
    for (float& c : w.colour) c = std::clamp(c, 0.0f, 1.0f);
}

// ---- heat, rate of fire, lockout ----
struct HeatState {
    float heat = 0.0f;             // 0..1
    float shotTimer = 0.0f;        // seconds until the next shot is allowed
    float lockout = 0.0f;          // seconds of lockout left
    bool overheated = false;
};

// Recovery needs the lockout over AND the heat back below this (so a weapon does not come back at 100% and lock straight out again).
constexpr float kRecoverHeat = 0.6f;

// Time passes: cool down, run the shot timer and the lockout; recover when both allow it.
inline void tickHeat(HeatState& s, const WeaponDef& w, float dt) {
    if (dt <= 0.0f) return;
    s.heat = std::max(0.0f, s.heat - w.cooldownRate * dt);
    s.shotTimer = std::max(0.0f, s.shotTimer - dt);
    if (s.overheated) {
        s.lockout = std::max(0.0f, s.lockout - dt);
        if (s.lockout <= 0.0f && s.heat <= kRecoverHeat) s.overheated = false;
    }
}

inline bool canFire(const HeatState& s) { return !s.overheated && s.shotTimer <= 0.0f; }

// Returns true if this addition tipped the weapon into overheat.
inline bool addHeat(HeatState& s, const WeaponDef& w, float amount) {
    s.heat = std::min(1.0f, s.heat + std::max(0.0f, amount));
    if (s.heat >= 1.0f && !s.overheated) { s.overheated = true; s.lockout = w.lockoutSeconds; return true; }
    return false;
}

// One shot of a projectile weapon: false (nothing happens) if it cannot fire; else heat and the shot timer are charged. `tipped` says it overheated.
inline bool fireShot(HeatState& s, const WeaponDef& w, bool& tipped) {
    tipped = false;
    if (!canFire(s)) return false;
    s.shotTimer = w.rateOfFire > 0.0f ? 1.0f / w.rateOfFire : 0.0f;
    tipped = addHeat(s, w, w.heatPerShot);
    return true;
}

// Beam: firing for `dt` seconds. False if locked out.
inline bool fireBeam(HeatState& s, const WeaponDef& w, float dt, bool& tipped) {
    tipped = false;
    if (s.overheated) return false;
    tipped = addHeat(s, w, w.heatPerSecond * dt);
    return true;
}

// ---- geometry ----
inline Vec3d sub(const Vec3d& a, const Vec3d& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3d add(const Vec3d& a, const Vec3d& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3d mul(const Vec3d& a, double k) { return {a.x * k, a.y * k, a.z * k}; }
inline double dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double length(const Vec3d& a) { return std::sqrt(dot(a, a)); }

// Does the segment p0 -> p1 touch the sphere? On a hit `t` (0..1) is the first touch along the segment (0 if p0 is already inside).
// Swept, so a fast bolt cannot tunnel through a small rock.
inline bool segmentSphere(const Vec3d& p0, const Vec3d& p1, const Vec3d& c, double r, double& t) {
    Vec3d d = sub(p1, p0), f = sub(p0, c);
    double cc = dot(f, f) - r * r;
    if (cc <= 0.0) { t = 0.0; return true; }                      // starts inside
    double a = dot(d, d);
    if (a < 1e-18) return false;                                  // no motion, still outside
    double b = 2.0 * dot(f, d), disc = b * b - 4.0 * a * cc;
    if (disc < 0.0) return false;                                 // never close enough (a tangent graze counts as a hit: disc == 0)
    double s = std::sqrt(disc), t0 = (-b - s) / (2.0 * a);
    if (t0 < 0.0 || t0 > 1.0) return false;                       // touches only before or after this step
    t = t0;
    return true;
}

// Distance along a ray (unit direction) to the sphere, within maxRange. False if it misses or is too far / behind.
inline bool raySphere(const Vec3d& origin, const Vec3d& dir, double maxRange, const Vec3d& c, double r, double& dist) {
    Vec3d f = sub(origin, c);
    double b = dot(f, dir), cc = dot(f, f) - r * r;
    if (cc <= 0.0) { dist = 0.0; return true; }
    double disc = b * b - cc;
    if (disc < 0.0) return false;
    double t = -b - std::sqrt(disc);
    if (t < 0.0 || t > maxRange) return false;
    dist = t;
    return true;
}

// ---- a fixed-capacity bolt pool ----
struct BoltPool {
    std::vector<double> px, py, pz;
    std::vector<float> vx, vy, vz, life, damage;
    std::vector<int> shooter;
    int n = 0, capacity = 0;
    long dropped = 0;

    void init(int cap) {
        capacity = std::max(0, cap);
        px.assign(capacity, 0); py.assign(capacity, 0); pz.assign(capacity, 0);
        vx.assign(capacity, 0); vy.assign(capacity, 0); vz.assign(capacity, 0);
        life.assign(capacity, 0); damage.assign(capacity, 0); shooter.assign(capacity, 0);
        n = 0; dropped = 0;
    }
    // false (counted as dropped) when full
    bool spawn(const Vec3d& pos, const Vec3d& vel, float lifetime, float dmg, int who) {
        if (n >= capacity) { dropped++; return false; }
        int i = n++;
        px[i] = pos.x; py[i] = pos.y; pz[i] = pos.z; vx[i] = (float)vel.x; vy[i] = (float)vel.y; vz[i] = (float)vel.z;
        life[i] = lifetime; damage[i] = dmg; shooter[i] = who;
        return true;
    }
    // swap-remove: the last live bolt takes slot i (so iterate backwards or re-check slot i)
    void remove(int i) {
        int last = --n;
        if (i != last) {
            px[i] = px[last]; py[i] = py[last]; pz[i] = pz[last]; vx[i] = vx[last]; vy[i] = vy[last]; vz[i] = vz[last];
            life[i] = life[last]; damage[i] = damage[last]; shooter[i] = shooter[last];
        }
    }
};

// A bolt's start velocity: the shooter's velocity plus the aim direction (unit) times the muzzle speed. Newtonian: nothing is "relative to the ship" once fired.
inline Vec3d boltVelocity(const Vec3d& shooterVel, const Vec3d& dir, double speed) { return add(shooterVel, mul(dir, speed)); }

// The bolt's motion over dt: new position and the segment start (= the old position).
inline Vec3d boltStep(const Vec3d& pos, const Vec3d& vel, double dt) { return add(pos, mul(vel, dt)); }

// Ship recoil: the velocity after firing along `dir` (unit): pushed backwards by `recoil` m/s times the tunable scale.
inline Vec3d recoilVelocity(const Vec3d& shipVel, const Vec3d& dir, double recoil, double scale) { return sub(shipVel, mul(dir, recoil * scale)); }

// ---- aim: a random unit direction inside a cone around `axis` (unit) ----
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 1) : s(seed ? seed * 2654435761u | 1u : 2463534242u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }
};

inline Vec3d spreadDirection(Rng& rng, const Vec3d& axis, double spreadDeg) {
    if (spreadDeg <= 0.0) return axis;
    Vec3d ref = std::fabs(axis.y) < 0.9 ? Vec3d{0, 1, 0} : Vec3d{1, 0, 0};
    Vec3d u{axis.y * ref.z - axis.z * ref.y, axis.z * ref.x - axis.x * ref.z, axis.x * ref.y - axis.y * ref.x};
    double ul = length(u); u = mul(u, 1.0 / ul);
    Vec3d w{axis.y * u.z - axis.z * u.y, axis.z * u.x - axis.x * u.z, axis.x * u.y - axis.y * u.x};
    double maxA = spreadDeg * 3.14159265358979 / 180.0, cosT = 1.0 - rng.f() * (1.0 - std::cos(maxA));
    double sinT = std::sqrt(std::max(0.0, 1.0 - cosT * cosT)), phi = rng.f() * 6.28318530717959;
    return add(add(mul(axis, cosT), mul(u, std::cos(phi) * sinT)), mul(w, std::sin(phi) * sinT));
}

// The muzzle position in the world: ship position + the offset in the ship frame (x right, y up, z forward).
inline Vec3d muzzlePosition(const Vec3d& shipPos, const Vec3d& fwd, const Vec3d& up, const float* offset) {
    Vec3d right{fwd.y * up.z - fwd.z * up.y, fwd.z * up.x - fwd.x * up.z, fwd.x * up.y - fwd.y * up.x};
    return add(shipPos, add(add(mul(right, offset[0]), mul(up, offset[1])), mul(fwd, offset[2])));
}

} // namespace combat
