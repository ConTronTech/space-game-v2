#pragma once
// Pure ship rules: no SDL, no GL, no engine. Unit-tested in package/tests/test_ship_rules.cpp.
// ship_core.cpp owns the state and events; the numbers and formulas live here.
#include <algorithm>
#include <array>
#include <string>
#include "engine/math.h"

namespace ship::rules {

// ---- double-precision position (docs/PRECISION.md) ----
// The ship's position is a double; its velocity stays a float (m/s-scale values lose nothing). One fixed step of flight: position += velocity * dt,
// the product and the sum in double, so the step is exact to ~1e-16 relative wherever the ship is. (A float position at 5e9 moves in 512 m jumps.)
inline void integrate(engine::Vec3d& pos, const engine::Vec3& vel, float dt) {
    pos.x += (double)vel.x * dt; pos.y += (double)vel.y * dt; pos.z += (double)vel.z * dt;
}
inline engine::Vec3d toD(const engine::Vec3& v) { return {v.x, v.y, v.z}; }
inline engine::Vec3 toF(const engine::Vec3d& v) { return {(float)v.x, (float)v.y, (float)v.z}; }
inline engine::Vec3d lerpD(const engine::Vec3d& a, const engine::Vec3d& b, double t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }
// b + v (a small float offset added in double)
inline engine::Vec3d offsetD(const engine::Vec3d& b, const engine::Vec3& v) { return {b.x + v.x, b.y + v.y, b.z + v.z}; }

// The mutable part of the ship that damage / healing / fuel act on.
struct Vitals {
    float hp = 100.0f, maxHp = 100.0f;
    float shield = 0.0f, maxShield = 200.0f;
    bool shieldInstalled = false, shieldEnabled = false;
    float warpFuel = 100.0f, maxWarpFuel = 100.0f;
    bool alive = true;
};

struct DamageResult {
    float toHull = 0;            // what reached the hull
    float absorbed = 0;          // what the shield took
    bool shieldBroken = false;   // shield ran out on this hit (it is gone for good)
    bool died = false;           // hp reached 0 on this hit
};

// Shield absorbs first (only while installed+enabled and > 0); a broken shield is removed. Ignored when dead / amount <= 0.
inline DamageResult applyDamage(Vitals& v, float amount) {
    DamageResult r;
    if (!v.alive || amount <= 0.0f) return r;
    if (v.shieldEnabled && v.shield > 0.0f) {
        if (amount <= v.shield) { v.shield -= amount; r.absorbed = amount; return r; }
        r.absorbed = v.shield;
        amount -= v.shield;
        v.shield = 0.0f;
        v.shieldEnabled = false;
        v.shieldInstalled = false;
        r.shieldBroken = true;
    }
    r.toHull = amount;
    v.hp -= amount;
    if (v.hp <= 0.0f) { v.hp = 0.0f; v.alive = false; r.died = true; }
    return r;
}

// Instant death (sun...): hp 0, shield irrelevant. Returns false if already dead.
inline bool kill(Vitals& v) {
    if (!v.alive) return false;
    v.hp = 0.0f; v.alive = false;
    return true;
}

// Passive regeneration, alive only, clamped to maxHp.
inline void regen(Vitals& v, float hpPerSec, float dt) {
    if (!v.alive || v.hp >= v.maxHp) return;
    v.hp = std::min(v.maxHp, v.hp + hpPerSec * dt);
}

inline void heal(Vitals& v, float amount) {
    if (!v.alive || amount <= 0.0f) return;
    v.hp = std::min(v.maxHp, v.hp + amount);
}

inline void addWarpFuel(Vitals& v, float amount) {
    if (amount <= 0.0f) return;
    v.warpFuel = std::min(v.maxWarpFuel, v.warpFuel + amount);
}

// false (and nothing taken) if there is not enough. `emptied` is set when this took the tank from >0 to 0.
inline bool consumeWarpFuel(Vitals& v, float amount, bool& emptied) {
    emptied = false;
    if (amount < 0.0f || v.warpFuel < amount) return false;
    float before = v.warpFuel;
    v.warpFuel -= amount;
    if (v.warpFuel <= 1e-6f) v.warpFuel = 0.0f;
    emptied = before > 0.0f && v.warpFuel == 0.0f;
    return true;
}

// Hull plating: maxHp grows, capped (old game: base 100 + at most 100 bonus). Like the old game, current hp is not topped up.
inline void addMaxHp(Vitals& v, float amount, float cap) {
    if (amount <= 0.0f) return;
    v.maxHp = std::min(std::max(cap, v.maxHp), v.maxHp + amount);
}

// Shield generator: installing fills the shield; removing clears it.
inline void installShield(Vitals& v, bool enabled) {
    v.shieldInstalled = enabled;
    v.shieldEnabled = enabled;
    v.shield = enabled ? v.maxShield : 0.0f;
}

// ---- collision damage (old game: speedFactor * baseDamage, minimum 1 HP scratch) ----
struct CollisionParams {
    float refSpeed = 200.0f;       // closing speed that counts as factor 1.0 (old MAX_SPEED)
    float asteroidSmall = 10.0f;   // radius < 5
    float asteroidMed = 20.0f;     // radius < 30
    float asteroidBig = 35.0f;
    float planet = 50.0f;
    float sun = 9999.0f;           // lethal damage of a sun hit when sunKills
    bool sunKills = true;          // false: the sun hurts like a planet (tiered by speed)
    float other = 10.0f;
    float minDamage = 1.0f;
    float minSpeed = 3.0f;         // ship.damage_min_speed: a hit slower than this (closing speed, m/s) is a scrape: no damage, no sound, no bounce
    float cooldown = 0.6f;         // ship.contact_cooldown: seconds before the SAME body can damage the ship again
};

inline float baseDamage(const std::string& kind, float radius, const CollisionParams& p) {
    if (kind == "asteroid") return radius < 5.0f ? p.asteroidSmall : radius < 30.0f ? p.asteroidMed : p.asteroidBig;
    if (kind == "planet" || kind == "moon") return p.planet;
    if (kind == "sun") return p.sunKills ? p.sun : p.planet;
    return p.other;
}

inline float collisionDamage(const std::string& kind, float otherRadius, float closingSpeed, const CollisionParams& p) {
    if (kind == "sun" && p.sunKills) return p.sun;                 // lethal at any speed
    float factor = p.refSpeed > 0.0f ? closingSpeed / p.refSpeed : 0.0f;
    return std::max(p.minDamage, factor * baseDamage(kind, otherRadius, p));
}

// Damage of a touch: the old formula above the scrape threshold, nothing below it. The sun still kills at any speed.
inline float contactDamage(const std::string& kind, float otherRadius, float closingSpeed, const CollisionParams& p) {
    if (kind == "sun" && p.sunKills) return p.sun;
    if (!(closingSpeed >= p.minSpeed)) return 0.0f;                 // scrape (also NaN)
    return collisionDamage(kind, otherRadius, closingSpeed, p);
}

// The ship's velocity after a contact whose surface normal n points from the ship to the other body and whose closing speed (relative to that body) is `closing`:
//   real impact (closing >= minSpeed): bounce, the closing component is reflected (1 + bounce) times  - exactly the old response
//   resting / scraping contact:        the closing component is removed after the pushout, the ship slides along the surface and does not jitter back in
//   separating (closing <= 0):         unchanged
inline engine::Vec3 velocityAfterContact(const engine::Vec3& vel, const engine::Vec3& n, float closing, float bounce, float minSpeed) {
    if (!(closing > 0.0f)) return vel;
    float k = closing >= minSpeed ? (1.0f + bounce) * closing : closing;
    return vel - n * k;
}

// "The same body only hurts once per cooldown": a few slots (asteroids, planets, stations touched at the same time), fixed storage, no allocation.
class ContactCooldown {
public:
    void tick(float dt) {
        for (auto& e : e_) if (e.id >= 0) { e.t -= dt; if (e.t <= 0.0f) e.id = -1; }
    }
    bool ready(int bodyId) const {
        for (const auto& e : e_) if (e.id == bodyId && e.t > 0.0f) return false;
        return true;
    }
    void arm(int bodyId, float seconds) {
        if (seconds <= 0.0f) return;
        Entry* slot = nullptr;
        for (auto& e : e_) if (e.id == bodyId) { slot = &e; break; }
        if (!slot) for (auto& e : e_) if (e.id < 0) { slot = &e; break; }
        if (!slot) slot = &*std::min_element(e_.begin(), e_.end(), [](const Entry& a, const Entry& b) { return a.t < b.t; });   // all busy: replace the one about to expire
        slot->id = bodyId; slot->t = seconds;
    }
private:
    struct Entry { int id = -1; float t = 0; };
    std::array<Entry, 8> e_{};
};

} // namespace ship::rules
