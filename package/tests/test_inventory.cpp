#include <fstream>
#include <sstream>
#include "engine/json.h"
#include "gameplay/crafting/crafting_data.h"
#include "gameplay/inventory/inventory_rules.h"
#include "tests/test.h"

namespace {
using namespace gameplay;
// the shipped ore stack caps on a grid (default 8 x 12 = 96 slots)
Cargo makeCargo(int cols = kDefaultGridCols, int rows = kDefaultGridRows) { Cargo c; c.setResources(defaultResources()); c.setGrid(cols, rows); return c; }
int slotsOf(const Cargo& c, const std::string& id) { int n = 0; for (auto& s : c.slots()) if (s.id == id) n++; return n; }
} // namespace

TEST(inventory_grid_is_unified_and_its_slot_count_is_fixed) {
    Cargo c = makeCargo();
    CHECK_EQ(c.slotCount(), 96); CHECK_EQ(c.columns(), 8); CHECK_EQ(c.rows(), 12);
    CHECK_EQ(c.usedSlots(), 0); CHECK_EQ(c.freeSlots(), 96);
    c.add("iron", 5); c.add("repair_kit", 1); c.add("uranium", 3);                       // ores and items share the SAME grid
    CHECK_EQ(c.usedSlots(), 3);
    CHECK_EQ(c.slots()[0].id, std::string("iron")); CHECK_EQ(c.slots()[1].id, std::string("repair_kit")); CHECK_EQ(c.slots()[2].id, std::string("uranium"));
    for (float m : {1.0f, 2.0f, 4.0f, 6.0f, 0.5f}) { c.setStackMult(m); CHECK_EQ(c.slotCount(), 96); CHECK_EQ(c.usedSlots(), 3); }   // levels never change the grid
    Cargo small = makeCargo(3, 2); CHECK_EQ(small.slotCount(), 6);
    Cargo bad = makeCargo(0, -4); CHECK_EQ(bad.slotCount(), 1);                           // nonsense sizes clamp to at least one slot
}

TEST(inventory_stack_cap_is_the_base_cap_times_the_level_multiplier) {
    Cargo c = makeCargo();
    c.setItemStack("hull_plating", 4);
    CHECK_EQ(c.stackCap("iron"), 100); CHECK_EQ(c.stackCap("uranium"), 10); CHECK_EQ(c.stackCap("gold"), 50);
    CHECK_EQ(c.stackCap("rock"), kRockCap);                                               // the filler stacks too
    CHECK_EQ(c.stackCap("repair_kit"), kDefaultItemStack);                                // an item without stack_cap: 10
    CHECK_EQ(c.stackCap("mystery"), kDefaultItemStack);                                   // an unknown id is a raw item stack
    CHECK_EQ(c.stackCap("hull_plating"), 4);
    auto lv = defaultCargoLevels();
    CHECK_EQ((int)lv.size(), 4);
    const int uranium[4] = {10, 20, 40, 60}, iron[4] = {100, 200, 400, 600}, plating[4] = {4, 8, 16, 24};
    for (int i = 0; i < 4; i++) {
        c.setStackMult(lv[(size_t)i].stackMult);
        CHECK_EQ(c.stackCap("uranium"), uranium[i]); CHECK_EQ(c.stackCap("iron"), iron[i]); CHECK_EQ(c.stackCap("hull_plating"), plating[i]);
        CHECK_EQ(c.stackCapAt("uranium", lv[(size_t)i].stackMult), uranium[i]);
    }
    c.setStackMult(0.05f); CHECK_EQ(c.stackCap("uranium"), 1);                             // a positive base never scales below 1 ...
    c.setStackMult(-3); CHECK_EQ(c.stackCap("iron"), 0); CHECK_EQ(c.add("iron", 5), 0);  // ... but a zero / negative multiplier is a closed hold
    c.setItemStack("feather", 0); c.setStackMult(1); CHECK_EQ(c.stackCap("feather"), 0);  // base 0 = cannot be carried
    CHECK_EQ(clampLevel(-2, 4), 0); CHECK_EQ(clampLevel(2, 4), 2); CHECK_EQ(clampLevel(50, 4), 3); CHECK_EQ(clampLevel(1, 0), 0);
    CHECK_EQ(itemBaseStack(5, 1), 5); CHECK_EQ(itemBaseStack(0, 1), 10); CHECK_EQ(itemBaseStack(0, 4), 2); CHECK_EQ(itemBaseStack(0, 50), 1); CHECK_EQ(itemBaseStack(0, 0), 10);
}

TEST(inventory_add_tops_up_existing_stacks_then_spills_into_empty_slots) {
    Cargo c = makeCargo();
    CHECK_EQ(c.add("uranium", 7), 7); CHECK_EQ(slotsOf(c, "uranium"), 1);
    CHECK_EQ(c.add("iron", 30), 30);
    CHECK_EQ(c.add("uranium", 25), 25);                                                   // 3 top up slot 0, then 10 + 10 + 2 spill into new slots
    CHECK_EQ(c.count("uranium"), 32); CHECK_EQ(slotsOf(c, "uranium"), 4);
    CHECK_EQ(c.slots()[0].amount, 10); CHECK_EQ(c.slots()[1].id, std::string("iron"));
    CHECK_EQ(c.slots()[2].amount, 10); CHECK_EQ(c.slots()[3].amount, 10); CHECK_EQ(c.slots()[4].amount, 2);
    c.discardSlot(2, 0);                                                                  // a hole in the middle ...
    CHECK_EQ(c.add("uranium", 9), 9);                                                     // ... partial stacks are topped up FIRST (slot 4: 2 -> 10), then the hole
    CHECK_EQ(c.slots()[4].amount, 10); CHECK_EQ(c.slots()[2].id, std::string("uranium")); CHECK_EQ(c.slots()[2].amount, 1);
    CHECK_EQ(c.room("uranium"), 9 + 91 * 10);                                             // room = partial slot + every empty slot at a full stack
}

TEST(inventory_a_full_grid_refuses_and_partial_accept_is_exact) {
    Cargo c = makeCargo(2, 2);                                                            // 4 slots
    CHECK_EQ(c.add("uranium", 25), 25);                                                   // 10 + 10 + 5: three slots
    CHECK_EQ(c.add("iron", 250), 100);                                                    // only the last slot is left: one iron stack, rest refused
    CHECK_EQ(c.freeSlots(), 0);
    CHECK_EQ(c.add("gold", 1), 0);                                                        // nothing matching, nothing empty: refused (CargoFull in the module)
    CHECK_EQ(c.add("uranium", 99), 5);                                                    // but the partial uranium stack still takes its last 5
    CHECK_EQ(c.add("uranium", 1), 0); CHECK_EQ(c.room("uranium"), 0); CHECK_EQ(c.room("gold"), 0);
    CHECK(c.remove("iron", 100)); CHECK_EQ(c.freeSlots(), 1); CHECK_EQ(c.add("gold", 60), 50);   // discarding frees the slot again
}

TEST(inventory_raising_the_level_grows_stacks_not_slots) {
    Cargo c = makeCargo(2, 1);                                                            // 2 slots
    CHECK_EQ(c.add("uranium", 50), 20);                                                   // level 0: two stacks of 10
    auto lv = defaultCargoLevels();
    c.setStackMult(lv[3].stackMult);                                                      // top level: 60 per stack, still 2 slots
    CHECK_EQ(c.slotCount(), 2); CHECK_EQ(c.room("uranium"), 100);
    CHECK_EQ(c.add("uranium", 500), 100); CHECK_EQ(c.count("uranium"), 120);
    c.setStackMult(1);                                                                    // lowered: over-full slots are kept, never deleted, but take nothing more
    CHECK_EQ(c.count("uranium"), 120); CHECK_EQ(c.room("uranium"), 0); CHECK_EQ(c.add("uranium", 1), 0);
    CHECK(c.remove("uranium", 115)); CHECK_EQ(c.count("uranium"), 5);
}

TEST(inventory_bad_amounts_and_ids_are_harmless_and_remove_is_atomic_across_slots) {
    Cargo c = makeCargo();
    CHECK_EQ(c.add("iron", 0), 0); CHECK_EQ(c.add("iron", -5), 0); CHECK_EQ(c.add("", 10), 0);
    CHECK_EQ(c.usedSlots(), 0);
    CHECK(!c.remove("iron", 0)); CHECK(!c.remove("iron", -3)); CHECK(!c.remove("nothing", 1)); CHECK(!c.remove("", 1));
    CHECK_EQ(c.add("uranium", 2000000000), 960);                                          // absurd request: the whole grid of uranium, no overflow
    CHECK_EQ(c.freeSlots(), 0);
    CHECK(!c.remove("uranium", 961)); CHECK_EQ(c.count("uranium"), 960);                  // not enough: nothing removed
    c.clear();
    c.add("uranium", 25); c.add("iron", 3);                                               // uranium 10 | 10 | 5 | iron 3
    CHECK(c.remove("uranium", 7));                                                        // taken from the LAST uranium slots first: 5 -> gone, then 10 -> 8
    CHECK_EQ(c.slots()[0].amount, 10); CHECK_EQ(c.slots()[1].amount, 8); CHECK(c.slots()[2].id.empty());
    CHECK_EQ(c.slots()[3].id, std::string("iron"));
    CHECK(c.remove("uranium", 18)); CHECK_EQ(c.count("uranium"), 0); CHECK_EQ(c.usedSlots(), 1);   // emptied slots become empty
}

TEST(inventory_totals_keep_the_old_per_id_view_in_grid_order) {
    Cargo c = makeCargo();
    c.add("iron", 5); c.add("repair_kit", 1); c.add("copper", 3); c.add("iron", 200);    // iron spills into a new slot after copper
    auto s = c.totals();
    CHECK_EQ(s.size(), (size_t)3);
    CHECK_EQ(s[0].id, std::string("iron")); CHECK_EQ(s[0].amount, 205);
    CHECK_EQ(s[1].id, std::string("repair_kit")); CHECK_EQ(s[2].id, std::string("copper"));
    c.remove("repair_kit", 1);
    s = c.totals(); CHECK_EQ(s.size(), (size_t)2); CHECK_EQ(s[1].id, std::string("copper"));
    c.add("rock", 10);
    CHECK_EQ(c.totalUnits(), 208);                                                        // the filler rock is not counted
    std::vector<std::string> ids; c.countedResources(ids);
    CHECK_EQ(ids.size(), (size_t)8); CHECK_EQ(ids[0], std::string("iron")); CHECK(std::find(ids.begin(), ids.end(), "rock") == ids.end());
    CHECK(c.isResource("iron")); CHECK(c.isResource("rock")); CHECK(!c.isResource("repair_kit"));
}

TEST(inventory_slot_moves_merge_swap_and_discard) {
    Cargo c = makeCargo();
    c.add("uranium", 16); c.add("iron", 4);                                               // uranium 10 | uranium 6 | iron 4
    CHECK(c.moveSlot(2, 10));                                                             // onto an empty slot: moved
    CHECK(c.slots()[2].id.empty()); CHECK_EQ(c.slots()[10].id, std::string("iron"));
    CHECK(c.moveSlot(0, 10));                                                             // onto another id: swapped
    CHECK_EQ(c.slots()[0].id, std::string("iron")); CHECK_EQ(c.slots()[10].amount, 10);
    CHECK(c.moveSlot(1, 10));                                                             // same id onto a full stack: swapped (nothing to merge)
    CHECK_EQ(c.slots()[1].amount, 10); CHECK_EQ(c.slots()[10].amount, 6);
    c.discardSlot(1, 3);                                                                  // 10 -> 7
    CHECK(c.moveSlot(10, 1));                                                             // same id: merge up to the cap, the rest stays
    CHECK_EQ(c.slots()[1].amount, 10); CHECK_EQ(c.slots()[10].amount, 3);
    CHECK(!c.moveSlot(5, 6)); CHECK(!c.moveSlot(1, 1)); CHECK(!c.moveSlot(-1, 2)); CHECK(!c.moveSlot(0, 96));   // empty source / same / out of range
    std::string id;
    CHECK_EQ(c.discardSlot(10, 0, &id), 3); CHECK_EQ(id, std::string("uranium")); CHECK(c.slots()[10].id.empty());
    CHECK_EQ(c.discardSlot(10, 0), 0); CHECK_EQ(c.discardSlot(500, 0), 0); CHECK_EQ(c.discardSlot(-1, 0), 0);
    CHECK_EQ(c.discardSlot(0, 99), 4); CHECK_EQ(c.count("iron"), 0);                     // more than it holds = the whole slot
}

TEST(inventory_room_after_removing_ingredients_simulates_without_changing_anything) {
    Cargo c = makeCargo(2, 1);                                                            // 2 slots, both used
    c.add("iron", 5); c.add("uranium", 3);
    CHECK_EQ(c.room("repair_kit"), 0);
    CHECK_EQ(c.roomAfter({{"iron", 5}}, "repair_kit"), 10);                               // all the iron leaves: its slot frees up for the result
    CHECK_EQ(c.roomAfter({{"iron", 4}, {"uranium", 3}}, "repair_kit"), 10);               // one slot freed
    CHECK_EQ(c.roomAfter({{"iron", 4}}, "repair_kit"), 0);                                // a slot still holds 1 iron: no room
    CHECK_EQ(c.roomAfter({{"iron", 2}}, "iron"), 97);                                     // same id: its own slot has room
    CHECK_EQ(c.count("iron"), 5); CHECK_EQ(c.count("uranium"), 3);                        // nothing actually changed
}

TEST(inventory_loading_old_saves_auto_stacks_and_clips_overflow) {
    Cargo c = makeCargo(2, 2);                                                            // 4 slots
    std::vector<Stack> clipped;
    // an old v2 save: merged per-id stacks; bad entries dropped, duplicates merged by auto-stacking
    c.assign({{"iron", 90}, {"uranium", 15}, {"", 5}, {"copper", -4}, {"iron", 20}, {"gold", 60}}, &clipped);
    CHECK_EQ(c.count("iron"), 110);                                                       // iron 90, uranium 10 | 5, then iron 20: +10 tops slot 0, 10 spill into slot 3
    CHECK_EQ(c.slots()[0].amount, 100); CHECK_EQ(c.slots()[3].amount, 10);
    CHECK_EQ(c.count("uranium"), 15); CHECK_EQ(c.count("copper"), 0); CHECK_EQ(c.count("gold"), 0);
    CHECK_EQ(c.freeSlots(), 0);
    int lost = 0; for (auto& s : clipped) lost += s.amount;
    CHECK_EQ(lost, 60);                                                                   // the gold had no slot left: reported so the caller can log it
    // the JSON path the module reads (old format: no "version" key)
    std::string err; engine::Json old = engine::Json::parse(R"({"level":0,"stacks":[{"id":"iron","amount":50},{"id":"uranium","amount":25}]})", &err);
    std::vector<Stack> in;
    for (size_t i = 0; i < old["stacks"].size(); i++) in.push_back({old["stacks"].at(i)["id"].str(), (int)old["stacks"].at(i)["amount"].num(0)});
    CHECK_EQ((int)old["version"].num(1), 1); CHECK(!old["slots"].isArray());
    Cargo d = makeCargo(); d.assign(in, &clipped);
    CHECK_EQ(d.count("iron"), 50); CHECK_EQ(d.count("uranium"), 25); CHECK(clipped.empty());    // 96 slots: the old 25 uranium now fit (3 stacks)
    Cargo e = makeCargo(); e.assign({}, &clipped); CHECK_EQ(e.usedSlots(), 0); CHECK(clipped.empty());
    e.assign({{"iron", 2000000000}}, &clipped); CHECK_EQ(e.count("iron"), 9600);          // absurd numbers: clipped, no overflow
}

TEST(inventory_save_round_trip_keeps_the_slot_layout) {
    Cargo a = makeCargo(); a.add("iron", 12); a.add("repair_kit", 3); a.add("uranium", 17);
    a.moveSlot(0, 40);                                                                    // the player arranged the grid
    engine::Json sl = engine::Json::array();
    for (size_t i = 0; i < a.slots().size(); i++)
        if (!a.slots()[i].id.empty()) sl.push(engine::Json::object().set("slot", (int)i).set("id", a.slots()[i].id).set("amount", a.slots()[i].amount));
    engine::Json saved = engine::Json::object().set("version", kSaveVersion).set("level", 2).set("slots", sl);
    CHECK_EQ(kSaveVersion, 3);
    std::string err; engine::Json back = engine::Json::parse(saved.dump(), &err);
    CHECK_EQ((int)back["version"].num(1), 3); CHECK(back["slots"].isArray());
    std::vector<Cargo::PlacedStack> in;
    for (size_t i = 0; i < back["slots"].size(); i++) in.push_back({(int)back["slots"].at(i)["slot"].num(-1), {back["slots"].at(i)["id"].str(), (int)back["slots"].at(i)["amount"].num(0)}});
    Cargo b = makeCargo(); b.assignSlots(in);
    for (size_t i = 0; i < a.slots().size(); i++) { CHECK_EQ(b.slots()[i].id, a.slots()[i].id); CHECK_EQ(b.slots()[i].amount, a.slots()[i].amount); }
    // a damaged layout: a bad index, a clash and an over-cap stack are re-stacked instead of lost; only a truly full grid clips
    Cargo c = makeCargo(2, 1); std::vector<Stack> clipped;
    c.assignSlots({{5, {"iron", 3}}, {0, {"uranium", 4}}, {0, {"uranium", 2}}, {1, {"gold", 70}}}, &clipped);
    CHECK_EQ(c.slots()[0].id, std::string("uranium")); CHECK_EQ(c.slots()[0].amount, 6);   // the clash merged into slot 0 by auto-stacking
    CHECK_EQ(c.slots()[1].amount, 50);                                                    // gold clipped to its cap in place
    int lost = 0; for (auto& s : clipped) lost += s.amount;
    CHECK_EQ(lost, 3 + 20);                                                               // no room left for the iron or the 20 extra gold
}

TEST(inventory_give_flag_parsing) {
    auto g = parseGive("iron:100,gold:50,uranium:10");
    CHECK_EQ(g.size(), (size_t)3); CHECK_EQ(g[0].id, std::string("iron")); CHECK_EQ(g[0].amount, 100); CHECK_EQ(g[2].amount, 10);
    CHECK_EQ(parseGive("").size(), (size_t)0);
    auto b = parseGive("iron:abc,:5,gold:-3,,cobalt:7,lonely");
    CHECK_EQ(b.size(), (size_t)2); CHECK_EQ(b[0].id, std::string("cobalt")); CHECK_EQ(b[1].id, std::string("lonely")); CHECK_EQ(b[1].amount, 1);
    Cargo c = makeCargo();
    for (auto& s : parseGive("iron:100,gold:50,uranium:10")) c.add(s.id, s.amount);
    CHECK_EQ(c.count("iron"), 100); CHECK_EQ(c.count("gold"), 50); CHECK_EQ(c.count("uranium"), 10); CHECK_EQ(c.usedSlots(), 3);
}

TEST(inventory_shipped_data_is_consistent_with_the_rules_and_cannot_softlock) {
    auto load = [](const char* path) { std::ifstream f(path); std::stringstream ss; ss << f.rdbuf(); std::string e; return engine::Json::parse(ss.str(), &e); };
    engine::Json cargo = load("data/cargo.json"), ores = load("data/ores.json"), items = load("data/items.json"), recipes = load("data/recipes.json");
    CHECK(cargo.isObject() && ores.isObject() && items.isObject() && recipes.isObject());
    // cargo levels: rising stack_mult, level 0 is the base, and the built-in table matches
    auto builtinLv = defaultCargoLevels();
    float last = 0; int n = 0;
    for (const char* id : {"level0", "level1", "level2", "level3"}) {
        const engine::Json& l = cargo[id];
        CHECK(l.isObject());
        float m = (float)l["stack_mult"].num(0);
        CHECK(m > last); last = m;
        CHECK(std::fabs(m - builtinLv[(size_t)n].stackMult) < 1e-4f); n++;
    }
    CHECK_EQ(n, 4); CHECK(std::fabs(cargo["level0"]["stack_mult"].num(0) - 1.0) < 1e-6);
    // ores: every ore has a cargo_cap and the shipped table equals the built-in one
    std::vector<ResourceDef> shipped;
    for (auto& id : ores.keys()) { if (id.empty() || id[0] == '_') continue; CHECK(ores[id].has("cargo_cap")); shipped.push_back({id, (int)ores[id]["cargo_cap"].num(0), true}); }
    auto builtin = defaultResources();
    CHECK_EQ(shipped.size(), builtin.size());
    for (size_t i = 0; i < shipped.size() && i < builtin.size(); i++) { CHECK_EQ(shipped[i].id, builtin[i].id); CHECK_EQ(shipped[i].cap, builtin[i].cap); }
    // ANTI-SOFTLOCK lint: from an EMPTY level-0 grid every recipe's ingredients fit (on as many slots as they need) and the result can be carried
    Cargo c; c.setResources(shipped);
    for (auto& id : items.keys()) if (!id.empty() && id[0] != '_') c.setItemStack(id, itemBaseStack(items[id]["stack_cap"].num(0), items[id]["volume"].num(1.0)));
    for (auto& id : recipes.keys()) {
        if (id.empty() || id[0] == '_') continue;
        Recipe r = recipeFromJson(id, recipes[id]);
        int slotsNeeded = 0;
        for (auto& ing : r.ingredients) {
            int cap = c.stackCap(ing.id);
            CHECK(cap > 0);
            if (cap > 0) slotsNeeded += (ing.need + cap - 1) / cap;
        }
        CHECK(slotsNeeded <= c.slotCount());                                              // never a recipe that needs more than the grid can carry
        CHECK(c.stackCap(r.result) >= 1);                                                 // never an item that cannot be carried at all
    }
}

TEST(inventory_perks_set_once_and_round_trip) {
    gameplay::Perks p;
    CHECK(!p.has("ore_scanner"));
    CHECK(p.add("ore_scanner")); CHECK(p.has("ore_scanner"));
    CHECK(!p.add("ore_scanner"));                                       // a second USE is refused
    CHECK(!p.add(""));
    engine::Json arr = engine::Json::array();                         // the save format: "perks": ["ore_scanner"]
    for (auto& id : p.ids) arr.push(id);
    engine::Json back = engine::Json::parse(engine::Json::object().set("perks", arr).dump());
    gameplay::Perks q;
    for (size_t k = 0; k < back["perks"].size(); k++) q.add(back["perks"].at(k).str());
    CHECK(q.has("ore_scanner")); CHECK_EQ((int)q.ids.size(), 1);
}
