#pragma once
// Reading recipes and item effects from JSON (data/recipes.json, data/items.json).
#include <string>
#include "engine/json.h"
#include "gameplay/crafting/crafting_rules.h"

namespace gameplay {

inline Recipe recipeFromJson(const std::string& id, const engine::Json& j) {
    Recipe r;
    r.id = id;
    r.name = j["name"].str(id);
    r.result = j["result"].str(id);
    const engine::Json& ing = j["ingredients"];
    for (auto& key : ing.keys()) {
        int need = (int)ing[key].num(0);
        if (need > 0) r.ingredients.push_back({key, need});
    }
    // optional "requires_blueprint": "<id>" - anything but a non-blank string means no gate (trimmed; the name is resolved by the caller)
    const std::string bp = j["requires_blueprint"].str();
    size_t a = bp.find_first_not_of(" \t\r\n"), b = bp.find_last_not_of(" \t\r\n");
    if (a != std::string::npos) r.requiresBlueprint = bp.substr(a, b - a + 1);
    return r;
}

inline Effect effectFromJson(const engine::Json& item) {
    Effect e;
    const engine::Json& x = item["effect"];
    e.warpFuel = (float)x["warp_fuel"].num(0);
    e.hp = (float)x["hp"].num(0);
    e.hpFull = x["hp_full"].boolean(false);
    e.missiles = (int)x["missiles"].num(0);
    e.maxHp = (float)x["max_hp"].num(0);
    e.maxHpCap = (float)x["max_hp_cap"].num(200);
    e.shieldEnabled = x["shield_enabled"].boolean(false);
    e.hudOreLabels = x["hud_ore_labels"].boolean(false);
    e.anomalyScan = x["anomaly_scan"].boolean(false);
    e.permanent = item["permanent"].boolean(false);
    return e;
}

} // namespace gameplay
