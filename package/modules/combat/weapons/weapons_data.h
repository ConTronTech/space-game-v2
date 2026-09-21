#pragma once
// Reading weapon definitions from data/weapons.json (JSON side of weapons_rules.h; still no GL/SDL).
#include <algorithm>
#include <string>
#include <vector>
#include "combat/weapons/weapons_rules.h"
#include "engine/json.h"

namespace combat {

// Fields missing from the entry keep the values of `base` (built-in defaults); the result is sanitised. kind: "projectile" or "beam".
inline WeaponDef weaponFromJson(const std::string& id, const engine::Json& j, WeaponDef base = WeaponDef{}) {
    WeaponDef w = base;
    w.name = j["name"].str(id);
    std::string k = j["kind"].str(w.kind == Kind::Beam ? "beam" : w.kind == Kind::Missile ? "missile" : "projectile");
    w.kind = k == "beam" ? Kind::Beam : k == "missile" ? Kind::Missile : Kind::Projectile;
    auto f = [&](const char* key, float def) { return (float)j[key].num(def); };
    w.damage = f("damage", w.damage); w.speed = f("speed", w.speed); w.lifetime = f("lifetime", w.lifetime); w.range = f("range", w.range);
    w.rateOfFire = f("rate_of_fire", w.rateOfFire); w.heatPerShot = f("heat_per_shot", w.heatPerShot); w.heatPerSecond = f("heat_per_second", w.heatPerSecond);
    w.cooldownRate = f("cooldown_rate", w.cooldownRate); w.lockoutSeconds = f("overheat_lockout_seconds", w.lockoutSeconds);
    w.recoil = f("recoil", w.recoil); w.spread = f("spread", w.spread); w.beamDps = f("beam_dps", w.beamDps);
    for (int i = 0; i < 3; i++) { w.muzzle[i] = (float)j["muzzle"].at(i).num(w.muzzle[i]); w.colour[i] = (float)j["colour"].at(i).num(w.colour[i]); }
    if (w.kind == Kind::Missile) {                    // damage = the blast's centre damage, lifetime = self-destruct time
        MissileParams& m = w.missile;
        auto d = [&](const char* key, double def) { return j[key].num(def); };
        m.maxDamage = f("damage", m.maxDamage); m.lifetime = f("lifetime", m.lifetime); m.fuel = f("fuel_seconds", m.fuel);
        m.thrust = d("thrust", m.thrust); m.turnRateDeg = d("turn_rate", m.turnRateDeg); m.armDistance = d("arm_distance", m.armDistance);
        m.fuseRadius = d("fuse_radius", m.fuseRadius); m.navGain = d("nav_gain", m.navGain); m.launchKick = d("launch_kick", m.launchKick);
        m.blastRadius = d("blast_radius", m.blastRadius);
    }
    sanitize(w);
    return w;
}

// The save of combat/weapons: the missiles in the rack and the selected weapon.
inline engine::Json weaponsSave(int missiles, int selected) { return engine::Json::object().set("missiles", missiles).set("selected", selected); }
// Missing keys keep the current values; loaded values are clamped to the rack cap and the weapon list.
inline void weaponsLoad(const engine::Json& j, int maxMissiles, int weaponCount, int& missiles, int& selected) {
    missiles = std::clamp((int)j["missiles"].num(missiles), 0, std::max(0, maxMissiles));
    selected = std::clamp((int)j["selected"].num(selected), 0, std::max(0, weaponCount - 1));
}

} // namespace combat
