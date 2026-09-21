#pragma once
// Pure crafting rules: no GL, no SDL, no engine types. Unit-tested in package/tests/test_crafting.cpp.
//   * recipe check: all ingredients present AND room in the hold for the result (ingredients are consumed first, so their space counts as free)
//   * item effects: which ship changes an item makes, and when it is refused instead (already full, already installed, not implemented yet)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace gameplay {

struct Ingredient { std::string id; int need = 0; };
struct Recipe { std::string id, name, result; std::vector<Ingredient> ingredients; };

// ---- crafting ----
struct Decision { bool ok = false; std::string reason; };

// countOf(id) = how many are in the hold. Ingredients (ores) come out of their own resource holds; the RESULT needs room only in the GENERAL hold:
// generalFree = free volume in it now, isGeneral(id) says whether an ingredient lives there too (then its volume is freed for the result),
// volumeOf(id) = volume per item. `docked` only matters when requireDock is on.
inline Decision decideCraft(const Recipe& r, const std::function<int(const std::string&)>& countOf, const std::function<float(const std::string&)>& volumeOf,
                            float generalFree, float resultVolume, bool requireDock, bool docked, const std::function<bool(const std::string&)>& isGeneral = nullptr) {
    if (requireDock && !docked) return {false, "dock at a station to craft"};
    if (r.ingredients.empty()) return {false, "recipe has no ingredients"};
    for (auto& ing : r.ingredients) {
        int missing = ing.need - countOf(ing.id);
        if (ing.need > 0 && missing > 0) return {false, "missing " + std::to_string(missing) + " " + ing.id};
    }
    float freed = 0;
    for (auto& ing : r.ingredients) if (isGeneral && isGeneral(ing.id)) freed += (float)std::max(0, ing.need) * volumeOf(ing.id);
    if (generalFree + freed + 1e-4f < resultVolume) return {false, "general hold full (use or discard an item)"};
    return {true, "ok"};
}

// ---- item effects (from data/items.json "effect") ----
struct Effect {
    float warpFuel = 0;              // +warp fuel
    float hp = 0;                    // +hull points
    bool hpFull = false;             // heal to maximum
    int missiles = 0;                // not implemented yet
    float maxHp = 0;                 // permanent +max HP ...
    float maxHpCap = 200;            // ... up to this cap
    bool shieldEnabled = false;      // installs the shield generator
    bool hudOreLabels = false;       // not implemented yet
    bool permanent = false;
    bool any() const { return warpFuel != 0 || hp != 0 || hpFull || missiles != 0 || maxHp != 0 || shieldEnabled || hudOreLabels; }
};

struct ShipState {
    bool alive = true;
    float hp = 100, maxHp = 100;
    float warpFuel = 100, maxWarpFuel = 100;
    bool shieldInstalled = false;
};

// What using the item does: the ship changes to make (the caller applies them through IShip), or a refusal. A refused item is NOT consumed.
struct UsePlan {
    bool ok = false;
    std::string reason;              // the refusal, or a short description of what happened when ok
    float addFuel = 0, heal = 0, addMaxHp = 0;
    bool installShield = false;
};

inline UsePlan decideUse(const Effect& e, const ShipState& s) {
    UsePlan p;
    if (!s.alive) { p.reason = "ship destroyed"; return p; }
    std::string firstRefusal;
    auto refuse = [&](const std::string& why) { if (firstRefusal.empty()) firstRefusal = why; };
    std::string done;
    auto note = [&](const std::string& d) { if (!done.empty()) done += ", "; done += d; };
    auto num = [](float v) { char b[32]; std::snprintf(b, sizeof b, "%.0f", v); return std::string(b); };

    if (e.warpFuel > 0) {
        if (s.warpFuel >= s.maxWarpFuel - 1e-3f) refuse("warp fuel already full");
        else { p.addFuel = std::min(e.warpFuel, s.maxWarpFuel - s.warpFuel); note("+" + num(p.addFuel) + " warp fuel"); }
    }
    if (e.hpFull || e.hp > 0) {
        if (s.hp >= s.maxHp - 1e-3f) refuse("hull already at maximum");
        else {
            p.heal = e.hpFull ? s.maxHp - s.hp : std::min(e.hp, s.maxHp - s.hp);
            note("+" + num(p.heal) + " HP");
        }
    }
    if (e.maxHp > 0) {
        if (s.maxHp >= e.maxHpCap - 1e-3f) refuse("hull plating already at the maximum");
        else { p.addMaxHp = std::min(e.maxHp, e.maxHpCap - s.maxHp); note("max HP +" + num(p.addMaxHp)); }
    }
    if (e.shieldEnabled) {
        if (s.shieldInstalled) refuse("shield generator already installed");
        else { p.installShield = true; note("shield installed"); }
    }
    if (e.missiles != 0 || e.hudOreLabels) refuse("not available yet");           // nothing implements missiles or ore labels yet
    if (!e.any()) refuse("this item has no effect");

    bool did = p.addFuel > 0 || p.heal > 0 || p.addMaxHp > 0 || p.installShield;
    p.ok = did;
    p.reason = did ? done : (firstRefusal.empty() ? "nothing to do" : firstRefusal);
    return p;
}

} // namespace gameplay
