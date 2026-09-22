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
struct Recipe {
    std::string id, name, result; std::vector<Ingredient> ingredients;
    std::string requiresBlueprint;   // "" = anyone can craft it; else a blueprint id (gameplay::IBlueprints, docs/BLUEPRINTS.md)
    std::string blueprintName;       // display name for the refusal ("blueprint required: <name>"); filled by gameplay/crafting
};

// ---- crafting ----
struct Decision { bool ok = false; std::string reason; };

// countOf(id) = how many are in the hold. Ingredients (ores) come out of their own resource holds; the RESULT needs room only in the GENERAL hold:
// generalFree = free volume in it now, isGeneral(id) says whether an ingredient lives there too (then its volume is freed for the result),
// volumeOf(id) = volume per item. `docked` only matters when requireDock is on.
// hasBlueprint(id) = is that blueprint unlocked; NULL = gameplay/blueprints is not loaded, and then the blueprint gate does not exist (no softlock).
// The blueprint is checked first: docking or mining will not help without it.
inline Decision decideCraft(const Recipe& r, const std::function<int(const std::string&)>& countOf, const std::function<float(const std::string&)>& volumeOf,
                            float generalFree, float resultVolume, bool requireDock, bool docked, const std::function<bool(const std::string&)>& isGeneral = nullptr,
                            const std::function<bool(const std::string&)>& hasBlueprint = nullptr) {
    if (!r.requiresBlueprint.empty() && hasBlueprint && !hasBlueprint(r.requiresBlueprint))
        return {false, "blueprint required: " + (r.blueprintName.empty() ? r.requiresBlueprint : r.blueprintName)};
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
    int missiles = 0;                // missiles added to the rack (combat::IAmmo)
    float maxHp = 0;                 // permanent +max HP ...
    float maxHpCap = 200;            // ... up to this cap
    bool shieldEnabled = false;      // installs the shield generator
    bool hudOreLabels = false;       // the "ore_scanner" perk (IInventory::hasPerk); the HUD labels themselves come later
    bool anomalyScan = false;        // the "anomaly_scanner" perk (world/anomalies, docs/ANOMALIES.md)
    bool permanent = false;
    bool any() const { return warpFuel != 0 || hp != 0 || hpFull || missiles != 0 || maxHp != 0 || shieldEnabled || hudOreLabels || anomalyScan; }
};

struct ShipState {
    bool alive = true;
    float hp = 100, maxHp = 100;
    float warpFuel = 100, maxWarpFuel = 100;
    bool shieldInstalled = false;
    int missiles = 0, maxMissiles = 0;   // the missile rack; maxMissiles 0 = no rack (combat off)
    bool oreScanner = false;             // the perk is already set
    bool anomalyScanner = false;         // the "anomaly_scanner" perk is already set
};

// What using the item does: the ship changes to make (the caller applies them through IShip), or a refusal. A refused item is NOT consumed.
struct UsePlan {
    bool ok = false;
    std::string reason;              // the refusal, or a short description of what happened when ok
    float addFuel = 0, heal = 0, addMaxHp = 0;
    bool installShield = false;
    int addMissiles = 0;             // through combat::IAmmo::addMissiles
    bool setOreScanner = false;      // through IInventory::addPerk("ore_scanner")
    bool setAnomalyScanner = false;  // through IInventory::addPerk("anomaly_scanner")
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
    if (e.missiles > 0) {
        int fit = std::clamp(std::min(e.missiles, s.maxMissiles - s.missiles), 0, e.missiles);
        if (s.maxMissiles <= 0) refuse("no missile rack");
        else if (fit <= 0) refuse("missile rack full");
        else { p.addMissiles = fit; note("+" + std::to_string(fit) + " missiles"); }
    }
    if (e.hudOreLabels) {
        if (s.oreScanner) refuse("ore scanner already installed");
        else { p.setOreScanner = true; note("ore scanner installed"); }
    }
    if (e.anomalyScan) {
        if (s.anomalyScanner) refuse("anomaly scanner already installed");
        else { p.setAnomalyScanner = true; note("anomaly scanner installed"); }
    }
    if (!e.any()) refuse("this item has no effect");

    bool did = p.addFuel > 0 || p.heal > 0 || p.addMaxHp > 0 || p.installShield || p.addMissiles > 0 || p.setOreScanner || p.setAnomalyScanner;
    p.ok = did;
    p.reason = did ? done : (firstRefusal.empty() ? "nothing to do" : firstRefusal);
    return p;
}

} // namespace gameplay
