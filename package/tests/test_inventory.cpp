#include <fstream>
#include <sstream>
#include "engine/json.h"
#include "gameplay/crafting/crafting_data.h"
#include "gameplay/inventory/inventory_rules.h"
#include "tests/test.h"

namespace {
using namespace gameplay;
bool close(double a, double b, double tol = 1e-4) { return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b)); }
// the shipped ore holds and a 30-unit general hold
Cargo makeCargo(float general = 30.0f) { Cargo c; c.setResources(defaultResources()); c.setGeneralBase(general); return c; }
} // namespace

TEST(inventory_every_ore_has_its_own_hold_so_one_cannot_block_another) {
    Cargo c = makeCargo();
    CHECK_EQ(c.add("iron", 100), 100); CHECK_EQ(c.count("iron"), 100);
    CHECK_EQ(c.add("iron", 1), 0);                                       // iron is full ...
    CHECK_EQ(c.add("gold", 50), 50);                                     // ... and gold, uranium and crafting are untouched
    CHECK_EQ(c.add("uranium", 10), 10); CHECK_EQ(c.add("platinum", 10), 10); CHECK_EQ(c.add("crystal", 10), 10);
    CHECK_EQ(c.add("uranium", 1), 0);                                    // uranium's own cap is 10
    CHECK(close(c.generalFree(), 30));                                   // the general hold is still empty: items can be made
    CHECK_EQ(c.add("repair_kit", 5), 5);
    CHECK_EQ(c.add("copper", 999), 80);                                  // partial accept up to THAT pool's cap
    CHECK_EQ(c.add("copper", 999), 0);
    CHECK_EQ(c.resourceCap("iron"), 100); CHECK_EQ(c.resourceCap("gold"), 50); CHECK_EQ(c.resourceCap("uranium"), 10);
    CHECK_EQ(c.resourceCap("titanium"), 60); CHECK_EQ(c.resourceCap("cobalt"), 50); CHECK_EQ(c.resourceCap("platinum"), 10); CHECK_EQ(c.resourceCap("crystal"), 10);
    CHECK_EQ(c.resourceCap("rock"), kRockCap);                           // the worthless filler has a hold too
    CHECK_EQ(c.resourceCap("mystery"), 0);                               // not an ore: no resource hold
}

TEST(inventory_totals_are_the_sum_of_every_hold_and_every_cap) {
    Cargo c = makeCargo();
    CHECK(close(c.totalCapacity(), 100 + 80 + 60 + 50 + 50 + 10 + 10 + 10 + 30));           // 400: every ore's cap + the general hold
    CHECK(close(c.totalUsed(), 0));
    c.add("iron", 45); c.add("gold", 5); c.add("uranium", 3); c.add("repair_kit", 4);
    CHECK(close(c.totalUsed(), 45 + 5 + 3 + 4));                                            // "CARGO 57 / 400": just add everything up
    c.add("rock", 30);                                                                      // the filler is neither counted in the used total ...
    CHECK(close(c.totalUsed(), 57)); CHECK(close(c.totalCapacity(), 400));                  // ... nor in the capacity total
    c.setMultipliers(2.0f, 3.0f);
    CHECK(close(c.totalCapacity(), 2 * 370 + 3 * 30));                                      // levels multiply both parts: 830
    std::vector<std::string> ids; c.countedResources(ids);
    CHECK_EQ(ids.size(), (size_t)8); CHECK_EQ(ids[0], std::string("iron")); CHECK(std::find(ids.begin(), ids.end(), "rock") == ids.end());
}

TEST(inventory_only_ores_from_the_data_count_and_a_missing_cap_defaults_to_20) {
    Cargo c; c.setResources({{"iron", 100, true}, {"weird_ore", kDefaultResourceCap, true}});   // an ore without cargo_cap gets the default
    c.setGeneralBase(10);
    CHECK_EQ(c.resourceCap("weird_ore"), 20);
    CHECK(close(c.totalCapacity(), 100 + 20 + 10));                                          // rock is added as a filler hold but not counted
    CHECK_EQ(c.add("gold", 5), 5);                                                           // gold is NOT an ore of this table: an unknown id goes to the general hold
    CHECK(close(c.generalUsed(), 5)); CHECK(!c.isResource("gold")); CHECK(c.isResource("iron"));
}

TEST(inventory_general_hold_takes_items_by_volume_and_unknown_ids) {
    Cargo c = makeCargo(30);
    c.setVolume("engine_part", 4.0f); c.setVolume("feather", 0.0f);
    CHECK_EQ(c.add("engine_part", 5), 5); CHECK(close(c.generalUsed(), 20));
    CHECK_EQ(c.add("engine_part", 5), 2);                                                    // 10 units left: only 2 more fit
    CHECK_EQ(c.add("repair_kit", 5), 2);                                                     // 2 units of room left: a partial accept
    CHECK_EQ(c.add("repair_kit", 5), 0);                                                     // now the general hold is full: refused
    CHECK_EQ(c.add("feather", 500), 500);                                                    // zero volume always fits
    CHECK(close(c.generalFree(), 0));
    CHECK_EQ(c.add("mystery_thing", 2), 0);                                                  // an unknown id is a raw stack in the SAME general hold: full too
    c.remove("repair_kit", 2); CHECK_EQ(c.add("mystery_thing", 2), 2);                        // discarding makes room again
    CHECK(close(c.free("engine_part"), 0)); CHECK(close(c.capacity("engine_part"), 30));
    CHECK(close(c.volumeOf("iron"), 1.0)); c.setVolume("bad", -3.0f); CHECK(close(c.volumeOf("bad"), 0.0));
    CHECK(close(c.free("iron"), 100)); CHECK(close(c.used("iron"), 0));                      // per-pool queries: iron is a resource hold
}

TEST(inventory_bad_amounts_and_ids_are_harmless_and_remove_is_atomic) {
    Cargo c = makeCargo();
    CHECK_EQ(c.add("iron", 0), 0); CHECK_EQ(c.add("iron", -5), 0); CHECK_EQ(c.add("", 10), 0);
    CHECK_EQ(c.stacks().size(), (size_t)0);
    CHECK(!c.remove("iron", 0)); CHECK(!c.remove("iron", -3)); CHECK(!c.remove("nothing", 1));
    CHECK_EQ(c.add("iron", 2000000000), 100);                                                // absurd request: clamped, no overflow
    CHECK(!c.remove("iron", 101)); CHECK_EQ(c.count("iron"), 100);                           // not enough: nothing removed
    CHECK(c.remove("iron", 40)); CHECK_EQ(c.count("iron"), 60);
    CHECK(c.remove("iron", 60)); CHECK_EQ(c.stacks().size(), (size_t)0);                      // an emptied stack disappears
    CHECK_EQ(c.add("iron", 100), 100);                                                       // and its room is back
    c.setGeneralBase(-10); CHECK(close(c.generalCapacity(), 0));                            // a negative capacity is a zero-size hold
    CHECK_EQ(c.add("repair_kit", 1), 0);
}

TEST(inventory_stacks_keep_a_stable_order_across_pools) {
    Cargo c = makeCargo();
    c.add("iron", 5); c.add("repair_kit", 1); c.add("copper", 3); c.add("iron", 2);
    auto s = c.stacks();
    CHECK_EQ(s.size(), (size_t)3);
    CHECK_EQ(s[0].id, std::string("iron")); CHECK_EQ(s[0].amount, 7);
    CHECK_EQ(s[1].id, std::string("repair_kit")); CHECK_EQ(s[2].id, std::string("copper"));
    c.remove("repair_kit", 1);
    s = c.stacks(); CHECK_EQ(s.size(), (size_t)2); CHECK_EQ(s[0].id, std::string("iron")); CHECK_EQ(s[1].id, std::string("copper"));
}

TEST(inventory_level_multipliers_scale_ore_holds_and_the_general_hold_separately) {
    auto lv = defaultCargoLevels();
    CHECK_EQ((int)lv.size(), 4);
    const float mult[4] = {1, 2, 4, 8};
    for (int i = 0; i < 4; i++) { CHECK(close(lv[i].resourceMult, mult[i])); CHECK(close(lv[i].generalMult, mult[i])); }
    CHECK_EQ(clampLevel(-2, 4), 0); CHECK_EQ(clampLevel(2, 4), 2); CHECK_EQ(clampLevel(50, 4), 3); CHECK_EQ(clampLevel(1, 0), 0);
    Cargo c = makeCargo(30);
    c.add("uranium", 10); c.add("gold", 50);
    CHECK_EQ(c.add("uranium", 1), 0);
    c.setMultipliers(lv[2].resourceMult, lv[2].generalMult);                                 // level 2: the rare ores gain room too
    CHECK_EQ(c.resourceCap("uranium"), 40); CHECK_EQ(c.resourceCap("gold"), 200); CHECK(close(c.generalCapacity(), 120));
    CHECK_EQ(c.add("uranium", 100), 30); CHECK_EQ(c.count("uranium"), 40);
    c.setMultipliers(1.0f, 8.0f);                                                            // the two multipliers are independent
    CHECK_EQ(c.resourceCap("uranium"), 10); CHECK(close(c.generalCapacity(), 240));
    CHECK_EQ(c.count("uranium"), 40);                                                        // shrinking never deletes: the pool is just over-full until emptied
    CHECK_EQ(c.add("uranium", 1), 0); CHECK(close(c.free("uranium"), 0));
    c.setMultipliers(-5, -5); CHECK_EQ(c.resourceCap("iron"), 0); CHECK(close(c.generalCapacity(), 0));   // nonsense is clamped
}

TEST(inventory_loading_an_old_single_pool_save_puts_stacks_in_their_pools_and_clips_overflow) {
    Cargo c = makeCargo(30);
    std::vector<Stack> clipped;
    // an old save: one 100-unit pool with 90 iron, 40 gold (over gold's 50? no: fits) and 30 uranium (over its 10), plus 50 repair kits (over the general 30)
    c.assign({{"iron", 90}, {"gold", 40}, {"uranium", 30}, {"repair_kit", 50}, {"", 5}, {"copper", -4}, {"iron", 20}}, &clipped);
    CHECK_EQ(c.count("iron"), 100);                                       // 90 + 20 merged, clipped to 100
    CHECK_EQ(c.count("gold"), 40); CHECK_EQ(c.count("uranium"), 10); CHECK_EQ(c.count("repair_kit"), 30);
    CHECK_EQ(c.count("copper"), 0);                                       // a bad entry is dropped
    int lost = 0; for (auto& s : clipped) lost += s.amount;
    CHECK_EQ(lost, 10 + 20 + 20);                                         // iron 10 (the second stack), uranium 20, repair kits 20: reported so the caller can log it
    // the same data through JSON, the way the module reads a save (old format: no "version" key)
    std::string err; engine::Json old = engine::Json::parse(R"({"level":0,"stacks":[{"id":"iron","amount":50},{"id":"uranium","amount":25}]})", &err);
    std::vector<Stack> in;
    for (size_t i = 0; i < old["stacks"].size(); i++) in.push_back({old["stacks"].at(i)["id"].str(), (int)old["stacks"].at(i)["amount"].num(0)});
    CHECK_EQ((int)old["version"].num(1), 1);                              // a missing version means version 1
    Cargo d = makeCargo(); d.assign(in, &clipped);
    CHECK_EQ(d.count("iron"), 50); CHECK_EQ(d.count("uranium"), 10); CHECK_EQ(clipped.size(), (size_t)1); CHECK_EQ(clipped[0].amount, 15);
    Cargo e = makeCargo(); e.assign({}, &clipped); CHECK_EQ(e.stacks().size(), (size_t)0); CHECK(clipped.empty());         // empty / garbage input: fine
    e.assign({{"iron", 2000000000}}, &clipped); CHECK_EQ(e.count("iron"), 100);                                          // absurd numbers: clipped, no overflow
}

TEST(inventory_save_round_trip_and_version) {
    Cargo a = makeCargo(); a.add("iron", 12); a.add("repair_kit", 3); a.add("uranium", 7);
    engine::Json arr = engine::Json::array();
    for (auto& s : a.stacks()) arr.push(engine::Json::object().set("id", s.id).set("amount", s.amount));
    engine::Json saved = engine::Json::object().set("version", kSaveVersion).set("level", 2).set("stacks", arr);
    CHECK_EQ(kSaveVersion, 2);
    std::string err; engine::Json back = engine::Json::parse(saved.dump(), &err);
    CHECK_EQ((int)back["version"].num(1), 2); CHECK_EQ((int)back["level"].num(0), 2);
    std::vector<Stack> in;
    for (size_t i = 0; i < back["stacks"].size(); i++) in.push_back({back["stacks"].at(i)["id"].str(), (int)back["stacks"].at(i)["amount"].num(0)});
    Cargo b = makeCargo(); b.assign(in);
    CHECK_EQ(b.stacks().size(), (size_t)3);
    for (int i = 0; i < 3; i++) { CHECK_EQ(b.stacks()[i].id, a.stacks()[i].id); CHECK_EQ(b.stacks()[i].amount, a.stacks()[i].amount); }
}

TEST(inventory_give_flag_parsing) {
    auto g = parseGive("iron:100,gold:50,uranium:10");
    CHECK_EQ(g.size(), (size_t)3); CHECK_EQ(g[0].id, std::string("iron")); CHECK_EQ(g[0].amount, 100); CHECK_EQ(g[2].amount, 10);
    CHECK_EQ(parseGive("").size(), (size_t)0);
    auto b = parseGive("iron:abc,:5,gold:-3,,cobalt:7,lonely");
    CHECK_EQ(b.size(), (size_t)2); CHECK_EQ(b[0].id, std::string("cobalt")); CHECK_EQ(b[1].id, std::string("lonely")); CHECK_EQ(b[1].amount, 1);
    Cargo c = makeCargo();                                                   // --give=iron:100,gold:50,uranium:10 fills three holds and blocks nothing
    for (auto& s : parseGive("iron:100,gold:50,uranium:10")) c.add(s.id, s.amount);
    CHECK_EQ(c.count("iron"), 100); CHECK_EQ(c.count("gold"), 50); CHECK_EQ(c.count("uranium"), 10); CHECK_EQ(c.add("crystal", 10), 10);
}

TEST(inventory_shipped_data_is_consistent_with_the_rules_and_cannot_softlock) {
    auto load = [](const char* path) { std::ifstream f(path); std::stringstream ss; ss << f.rdbuf(); std::string e; return engine::Json::parse(ss.str(), &e); };
    engine::Json cargo = load("data/cargo.json"), ores = load("data/ores.json"), items = load("data/items.json"), recipes = load("data/recipes.json");
    CHECK(cargo.isObject() && ores.isObject() && items.isObject() && recipes.isObject());
    // cargo levels: rising, level 0 is the base, both multipliers present
    float lastR = 0, lastG = 0; int n = 0;
    for (const char* id : {"level0", "level1", "level2", "level3"}) {
        const engine::Json& l = cargo[id];
        CHECK(l.isObject());
        float r = (float)l["resource_mult"].num(0), g = (float)l["general_mult"].num(0);
        CHECK(r > lastR); CHECK(g > lastG); lastR = r; lastG = g; n++;
    }
    CHECK_EQ(n, 4); CHECK(close(cargo["level0"]["resource_mult"].num(0), 1.0)); CHECK(close(cargo["level3"]["general_mult"].num(0), 8.0));
    // ores: every ore has a cargo_cap and the shipped table equals the built-in one
    std::vector<ResourceDef> shipped;
    for (auto& id : ores.keys()) { if (id.empty() || id[0] == '_') continue; CHECK(ores[id].has("cargo_cap")); shipped.push_back({id, (int)ores[id]["cargo_cap"].num(0), true}); }
    auto builtin = defaultResources();
    CHECK_EQ(shipped.size(), builtin.size());
    for (size_t i = 0; i < shipped.size() && i < builtin.size(); i++) { CHECK_EQ(shipped[i].id, builtin[i].id); CHECK_EQ(shipped[i].cap, builtin[i].cap); }
    // ANTI-SOFTLOCK lint: every recipe can be crafted from an EMPTY start (each ingredient fits its hold) and its result fits the general hold
    Cargo c; c.setResources(shipped); c.setGeneralBase(30);
    for (auto& id : recipes.keys()) {
        if (id.empty() || id[0] == '_') continue;
        Recipe r = recipeFromJson(id, recipes[id]);
        for (auto& ing : r.ingredients) CHECK(ing.need <= c.resourceCap(ing.id));                 // never a recipe that needs more of an ore than the hold can carry
        float vol = (float)items[r.result]["volume"].num(1.0);
        CHECK(vol <= c.generalCapacity());                                                          // never an item too big for the general hold
    }
    // total capacity at level 0 with the shipped numbers: 100+80+60+50+50+10+10+10 + 30 = 400
    CHECK(close(c.totalCapacity(), 400));
}
