#pragma once
// Reading weapon definitions from data/weapons.json (JSON side of weapons_rules.h; still no GL/SDL).
#include <string>
#include <vector>
#include "combat/weapons/weapons_rules.h"
#include "engine/json.h"

namespace combat {

// Fields missing from the entry keep the values of `base` (built-in defaults); the result is sanitised. kind: "projectile" or "beam".
inline WeaponDef weaponFromJson(const std::string& id, const engine::Json& j, WeaponDef base = WeaponDef{}) {
    WeaponDef w = base;
    w.name = j["name"].str(id);
    std::string k = j["kind"].str(w.kind == Kind::Beam ? "beam" : "projectile");
    w.kind = k == "beam" ? Kind::Beam : Kind::Projectile;
    auto f = [&](const char* key, float def) { return (float)j[key].num(def); };
    w.damage = f("damage", w.damage); w.speed = f("speed", w.speed); w.lifetime = f("lifetime", w.lifetime); w.range = f("range", w.range);
    w.rateOfFire = f("rate_of_fire", w.rateOfFire); w.heatPerShot = f("heat_per_shot", w.heatPerShot); w.heatPerSecond = f("heat_per_second", w.heatPerSecond);
    w.cooldownRate = f("cooldown_rate", w.cooldownRate); w.lockoutSeconds = f("overheat_lockout_seconds", w.lockoutSeconds);
    w.recoil = f("recoil", w.recoil); w.spread = f("spread", w.spread); w.beamDps = f("beam_dps", w.beamDps);
    for (int i = 0; i < 3; i++) { w.muzzle[i] = (float)j["muzzle"].at(i).num(w.muzzle[i]); w.colour[i] = (float)j["colour"].at(i).num(w.colour[i]); }
    sanitize(w);
    return w;
}

} // namespace combat
