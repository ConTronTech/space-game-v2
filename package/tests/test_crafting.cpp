#include <fstream>
#include <map>
#include <sstream>
#include "gameplay/crafting/crafting_data.h"
#include "gameplay/crafting/crafting_rules.h"
#include "tests/test.h"

namespace {
using namespace gameplay;
bool close(double a, double b, double tol = 1e-4) { return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b)); }
Recipe fuelCell() { return {"warp_fuel_cell", "Warp Fuel Cell", "warp_fuel_cell", {{"iron", 5}, {"uranium", 3}}}; }
struct Hold {
    std::map<std::string, int> stacks; float free = 100;
    std::function<int(const std::string&)> count() { return [this](const std::string& k) { auto it = stacks.find(k); return it == stacks.end() ? 0 : it->second; }; }
};
auto vol1 = [](const std::string&) { return 1.0f; };
Decision craftCheck(const Recipe& r, Hold& h, float resultVol = 1.0f, bool requireDock = false, bool docked = false) {
    return decideCraft(r, h.count(), vol1, h.free, resultVol, requireDock, docked);
}
Effect effectOf(const char* json) { std::string e; auto j = engine::Json::parse(json, &e); return effectFromJson(j); }
} // namespace

TEST(crafting_recipe_needs_every_ingredient) {
    Hold h; h.stacks = {{"iron", 5}, {"uranium", 3}};
    auto d = craftCheck(fuelCell(), h);
    CHECK(d.ok);                                                         // exactly enough
    h.stacks["uranium"] = 2;
    d = craftCheck(fuelCell(), h); CHECK(!d.ok); CHECK_EQ(d.reason, std::string("missing 1 uranium"));   // partial ingredients
    h.stacks.clear();
    d = craftCheck(fuelCell(), h); CHECK(!d.ok); CHECK_EQ(d.reason, std::string("missing 5 iron"));       // the first missing one is named
    h.stacks = {{"iron", 500}, {"uranium", 500}};
    CHECK(craftCheck(fuelCell(), h).ok);                                 // more than enough
    Recipe empty{"x", "X", "x", {}}; CHECK(!craftCheck(empty, h).ok);   // a recipe without ingredients would be free items: refused
}

TEST(crafting_result_needs_room_only_in_the_general_hold) {
    Hold h; h.stacks = {{"iron", 5}, {"uranium", 3}};
    // ingredients are ores: they come out of their own resource holds and free nothing in the general hold, so the result needs general room by itself
    h.free = 1; CHECK(craftCheck(fuelCell(), h, 1.0f).ok);
    h.free = 0;
    auto d = craftCheck(fuelCell(), h, 1.0f);
    CHECK(!d.ok); CHECK_EQ(d.reason, std::string("cargo full (use or discard an item)"));       // the exact wording the player sees
    h.free = 0.5f; CHECK(!craftCheck(fuelCell(), h, 1.0f).ok);
    h.free = 8; CHECK(craftCheck(fuelCell(), h, 8.0f).ok); CHECK(!craftCheck(fuelCell(), h, 8.5f).ok);
    // a full iron hold never matters: crafting checks the general hold only
    Hold ironFull; ironFull.stacks = {{"iron", 100}, {"uranium", 3}}; ironFull.free = 5;
    CHECK(craftCheck(fuelCell(), ironFull, 1.0f).ok);
    // an ingredient that lives in the general hold frees its own volume for the result
    Recipe fromItems{"combo", "Combo", "combo", {{"repair_kit", 2}}};
    Hold g; g.stacks = {{"repair_kit", 2}}; g.free = 0;
    auto isGeneral = [](const std::string&) { return true; };
    CHECK(decideCraft(fromItems, g.count(), vol1, 0.0f, 2.0f, false, false, isGeneral).ok);        // 2 leave, 2 arrive
    CHECK(!decideCraft(fromItems, g.count(), vol1, 0.0f, 3.0f, false, false, isGeneral).ok);
    CHECK(!decideCraft(fromItems, g.count(), vol1, 0.0f, 2.0f, false, false).ok);                    // without the hint the ingredients are ores: nothing freed
}

TEST(crafting_the_dock_rule_is_a_switch) {
    Hold h; h.stacks = {{"iron", 5}, {"uranium", 3}};
    CHECK(craftCheck(fuelCell(), h, 1.0f, false, false).ok);             // default: craft anywhere
    auto d = craftCheck(fuelCell(), h, 1.0f, true, false);
    CHECK(!d.ok); CHECK_EQ(d.reason, std::string("dock at a station to craft"));
    CHECK(craftCheck(fuelCell(), h, 1.0f, true, true).ok);               // docked: fine
}

TEST(crafting_effects_parse_from_the_shipped_items) {
    Effect fuel = effectOf(R"({"effect":{"warp_fuel":25}})");
    CHECK(close(fuel.warpFuel, 25)); CHECK(fuel.any()); CHECK(!fuel.permanent);
    Effect beacon = effectOf(R"({"effect":{"hp_full":true}})"); CHECK(beacon.hpFull);
    Effect plating = effectOf(R"({"permanent":true,"effect":{"max_hp":10,"max_hp_cap":200}})");
    CHECK(close(plating.maxHp, 10)); CHECK(close(plating.maxHpCap, 200)); CHECK(plating.permanent);
    Effect shield = effectOf(R"({"permanent":true,"effect":{"shield_enabled":true}})"); CHECK(shield.shieldEnabled);
    Effect none = effectOf(R"({"effect":{}})"); CHECK(!none.any());
    Effect missing = effectOf("{}"); CHECK(!missing.any());
}

TEST(crafting_shipped_data_files_are_consistent_with_the_rules) {
    auto load = [](const char* path) { std::ifstream f(path); std::stringstream ss; ss << f.rdbuf(); std::string e; return engine::Json::parse(ss.str(), &e); };
    engine::Json recipes = load("data/recipes.json"), items = load("data/items.json"), ores = load("data/ores.json");
    CHECK(recipes.isObject() && items.isObject() && ores.isObject());
    int n = 0;
    for (auto& id : recipes.keys()) {
        if (id.empty() || id[0] == '_') continue;
        Recipe r = recipeFromJson(id, recipes[id]);
        CHECK(!r.ingredients.empty());
        CHECK(items.has(r.result));                                      // every recipe makes a real item
        for (auto& ing : r.ingredients) CHECK(ores.has(ing.id) && ing.need > 0);
        CHECK(effectFromJson(items[r.result]).any());                    // and every crafted item does something (or is refused for a reason)
        n++;
    }
    CHECK(n >= 8);
}

TEST(crafting_use_warp_fuel_clamps_and_refuses_when_full) {
    Effect cell = effectOf(R"({"effect":{"warp_fuel":25}})");
    ShipState s; s.warpFuel = 10; s.maxWarpFuel = 100;
    UsePlan p = decideUse(cell, s);
    CHECK(p.ok); CHECK(close(p.addFuel, 25));
    s.warpFuel = 90; p = decideUse(cell, s);
    CHECK(p.ok); CHECK(close(p.addFuel, 10));                            // only what fits
    s.warpFuel = 100; p = decideUse(cell, s);
    CHECK(!p.ok); CHECK_EQ(p.reason, std::string("warp fuel already full")); CHECK(close(p.addFuel, 0));   // refused: the item is not consumed
    s.warpFuel = 99.9995f; p = decideUse(cell, s); CHECK(!p.ok);         // "full" within rounding
}

TEST(crafting_use_repair_and_beacon) {
    Effect kit = effectOf(R"({"effect":{"hp":30}})"), beacon = effectOf(R"({"effect":{"hp_full":true}})");
    ShipState s; s.hp = 50; s.maxHp = 100;
    CHECK(close(decideUse(kit, s).heal, 30)); CHECK(close(decideUse(beacon, s).heal, 50));
    s.hp = 90; CHECK(close(decideUse(kit, s).heal, 10));                 // clamps at max
    s.hp = 100; CHECK(!decideUse(kit, s).ok); CHECK_EQ(decideUse(beacon, s).reason, std::string("hull already at maximum"));
    s.alive = false; s.hp = 10; CHECK(!decideUse(kit, s).ok); CHECK_EQ(decideUse(kit, s).reason, std::string("ship destroyed"));
}

TEST(crafting_permanent_items_apply_once_or_refuse) {
    Effect shield = effectOf(R"({"permanent":true,"effect":{"shield_enabled":true}})");
    ShipState s;
    UsePlan p = decideUse(shield, s);
    CHECK(p.ok); CHECK(p.installShield);
    s.shieldInstalled = true; p = decideUse(shield, s);                  // a second one while the first is installed: refused
    CHECK(!p.ok); CHECK(!p.installShield); CHECK_EQ(p.reason, std::string("shield generator already installed"));
    Effect plating = effectOf(R"({"permanent":true,"effect":{"max_hp":10,"max_hp_cap":200}})");
    ShipState h; h.maxHp = 100; p = decideUse(plating, h); CHECK(p.ok); CHECK(close(p.addMaxHp, 10));
    h.maxHp = 195; p = decideUse(plating, h); CHECK(p.ok); CHECK(close(p.addMaxHp, 5));   // the last bit up to the cap
    h.maxHp = 200; p = decideUse(plating, h); CHECK(!p.ok); CHECK_EQ(p.reason, std::string("hull plating already at the maximum"));
}

TEST(crafting_missile_pack_and_ore_scanner) {
    Effect missiles = effectOf(R"({"effect":{"missiles":3}})"), scanner = effectOf(R"({"permanent":true,"effect":{"hud_ore_labels":true}})");
    ShipState s;
    UsePlan p = decideUse(missiles, s); CHECK(!p.ok); CHECK_EQ(p.reason, std::string("no missile rack"));   // combat off: no rack
    s.maxMissiles = 12; s.missiles = 0; p = decideUse(missiles, s); CHECK(p.ok); CHECK_EQ(p.addMissiles, 3);
    s.missiles = 11; p = decideUse(missiles, s); CHECK(p.ok); CHECK_EQ(p.addMissiles, 1);                 // the last one that fits
    s.missiles = 12; p = decideUse(missiles, s); CHECK(!p.ok); CHECK_EQ(p.addMissiles, 0); CHECK_EQ(p.reason, std::string("missile rack full"));
    p = decideUse(scanner, s); CHECK(p.ok); CHECK(p.setOreScanner);
    s.oreScanner = true; p = decideUse(scanner, s); CHECK(!p.ok); CHECK_EQ(p.reason, std::string("ore scanner already installed"));
    Effect none = effectOf("{}"); p = decideUse(none, s); CHECK(!p.ok); CHECK_EQ(p.reason, std::string("this item has no effect"));
    // an item with an applicable and a refused effect applies the applicable one
    Effect mixed = effectOf(R"({"effect":{"hp":30,"missiles":2}})"); s.hp = 10; p = decideUse(mixed, s);
    CHECK(p.ok); CHECK(close(p.heal, 30)); CHECK_EQ(p.addMissiles, 0);
}
