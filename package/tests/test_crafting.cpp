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

TEST(crafting_result_needs_room_but_ingredients_free_theirs) {
    Hold h; h.stacks = {{"iron", 5}, {"uranium", 3}}; h.free = 0;        // a completely full hold
    CHECK(craftCheck(fuelCell(), h, 1.0f).ok);                           // 8 units leave, 1 arrives: fits
    CHECK(craftCheck(fuelCell(), h, 8.0f).ok);                           // exactly the freed space
    auto d = craftCheck(fuelCell(), h, 9.0f);
    CHECK(!d.ok); CHECK_EQ(d.reason, std::string("cargo full"));         // a bulky result does not fit even after the ingredients leave
    Hold tight; tight.stacks = {{"iron", 5}, {"uranium", 3}}; tight.free = 2;
    CHECK(craftCheck(fuelCell(), tight, 10.0f).ok);                      // 2 free + 8 freed = 10
    CHECK(!craftCheck(fuelCell(), tight, 10.5f).ok);
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

TEST(crafting_unimplemented_effects_are_refused_honestly) {
    Effect missiles = effectOf(R"({"effect":{"missiles":3}})"), scanner = effectOf(R"({"permanent":true,"effect":{"hud_ore_labels":true}})");
    ShipState s;
    UsePlan p = decideUse(missiles, s); CHECK(!p.ok); CHECK_EQ(p.reason, std::string("not available yet"));
    p = decideUse(scanner, s); CHECK(!p.ok); CHECK_EQ(p.reason, std::string("not available yet"));
    Effect none = effectOf("{}"); p = decideUse(none, s); CHECK(!p.ok); CHECK_EQ(p.reason, std::string("this item has no effect"));
    // an item with an implemented and an unimplemented effect applies the implemented one
    Effect mixed = effectOf(R"({"effect":{"hp":30,"missiles":2}})"); s.hp = 10; p = decideUse(mixed, s);
    CHECK(p.ok); CHECK(close(p.heal, 30));
}
