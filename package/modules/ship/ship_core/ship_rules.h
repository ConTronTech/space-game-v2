#pragma once
// Pure ship rules: no SDL, no GL, no engine. Unit-tested in package/tests/test_ship_rules.cpp.
// ship_core.cpp owns the state and events; the numbers and formulas live here.
#include <algorithm>
#include <string>

namespace ship::rules {

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
    float sun = 9999.0f;
    float other = 10.0f;
    float minDamage = 1.0f;
};

inline float baseDamage(const std::string& kind, float radius, const CollisionParams& p) {
    if (kind == "asteroid") return radius < 5.0f ? p.asteroidSmall : radius < 30.0f ? p.asteroidMed : p.asteroidBig;
    if (kind == "planet" || kind == "moon") return p.planet;
    if (kind == "sun") return p.sun;
    return p.other;
}

inline float collisionDamage(const std::string& kind, float otherRadius, float closingSpeed, const CollisionParams& p) {
    float factor = p.refSpeed > 0.0f ? closingSpeed / p.refSpeed : 0.0f;
    return std::max(p.minDamage, factor * baseDamage(kind, otherRadius, p));
}

} // namespace ship::rules
