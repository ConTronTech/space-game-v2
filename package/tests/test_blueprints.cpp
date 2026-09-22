#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include "gameplay/blueprints/blueprints_api.h"
#include "gameplay/blueprints/blueprints_rules.h"
#include "gameplay/crafting/crafting_data.h"
#include "gameplay/crafting/crafting_rules.h"
#include "world/anomalies/anomaly_rules.h"
#include "tests/test.h"

namespace {
using namespace gameplay;
engine::Json parseB(const char* s) { std::string e; return engine::Json::parse(s, &e); }
engine::Json loadB(const char* path) { std::ifstream f(path); std::stringstream ss; ss << f.rdbuf(); std::string e; return engine::Json::parse(ss.str(), &e); }

// a stand-in for the gameplay/blueprints module (the rules' BlueprintSet behind the interface)
struct FakeBlueprints : IBlueprints {
    BlueprintSet set; int unlockCalls = 0;
    bool unlocked(const std::string& id) const override { return set.unlocked(id); }
    bool unlock(const std::string& id) override { unlockCalls++; return set.unlock(id); }
    void list(std::vector<std::string>& out) const override { out = set.list(); }
    std::string displayName(const std::string& id) const override { return blueprintTitle(id); }
};

Recipe gated() {
    Recipe r{"missile_pack", "Missile Pack", "missile_pack", {{"iron", 3}}};
    r.requiresBlueprint = "missile_pack"; r.blueprintName = "Missile Pack";
    return r;
}
Decision check(const Recipe& r, const std::function<bool(const std::string&)>& hasBp, int iron = 10, bool requireDock = false, bool docked = false) {
    auto count = [iron](const std::string& k) { return k == "iron" ? iron : 0; };
    auto vol = [](const std::string&) { return 1.0f; };
    return decideCraft(r, count, vol, 100.0f, 1.0f, requireDock, docked, nullptr, hasBp);
}
} // namespace

TEST(blueprints_set_unlock_list_and_twice_returns_false) {
    BlueprintSet s;
    CHECK(!s.unlocked("missile_pack"));
    CHECK(s.unlock("missile_pack"));
    CHECK(s.unlocked("missile_pack"));
    CHECK(!s.unlock("missile_pack"));                 // second time: already unlocked
    CHECK(!s.unlock("  missile_pack "));              // trimmed: the same id
    CHECK(!s.unlock("")); CHECK(!s.unlock("   "));    // empty ids are never unlocked
    CHECK(s.unlock("beacon"));
    auto l = s.list();
    CHECK_EQ(l.size(), (size_t)2); CHECK_EQ(l[0], std::string("beacon")); CHECK_EQ(l[1], std::string("missile_pack"));
}

TEST(blueprints_save_load_round_trip_and_bad_entries) {
    BlueprintSet a; a.unlock("missile_pack"); a.unlock("beacon");
    std::string text = a.toJson().dump();
    BlueprintSet b; b.unlock("stale");                // load replaces
    b.fromJson(parseB(text.c_str()));
    CHECK_EQ(b.size(), (size_t)2); CHECK(b.unlocked("missile_pack")); CHECK(b.unlocked("beacon")); CHECK(!b.unlocked("stale"));
    b.fromJson(parseB(R"({"unlocked":["x", 5, null, "", "x", " y "]})"));
    CHECK_EQ(b.size(), (size_t)2); CHECK(b.unlocked("x")); CHECK(b.unlocked("y"));
    b.fromJson(parseB("{}"));                         // an old save without the key: nothing unlocked
    CHECK_EQ(b.size(), (size_t)0);
}

TEST(blueprints_display_names) {
    CHECK_EQ(blueprintTitle("missile_pack"), std::string("Missile Pack"));
    CHECK_EQ(blueprintTitle("a__b-c"), std::string("A B C"));
    CHECK_EQ(blueprintName("missile_pack", parseB(R"({"name":"Ordnance Rack"})")), std::string("Ordnance Rack"));
    CHECK_EQ(blueprintName("missile_pack", parseB(R"({"name":7})")), std::string("Missile Pack"));
}

TEST(blueprints_crafting_gate_refuses_locked_and_allows_unlocked) {
    FakeBlueprints bp;
    auto has = [&](const std::string& k) { return bp.unlocked(k); };
    Decision d = check(gated(), has);
    CHECK(!d.ok); CHECK_EQ(d.reason, std::string("blueprint required: Missile Pack"));
    d = check(gated(), has, 0, true, false);          // named before docking / ingredients: those would not help
    CHECK_EQ(d.reason, std::string("blueprint required: Missile Pack"));
    bp.unlock("missile_pack");
    d = check(gated(), has); CHECK(d.ok);
    d = check(gated(), has, 1); CHECK(!d.ok); CHECK_EQ(d.reason, std::string("missing 2 iron"));   // the usual rules still apply
    Recipe plain = gated(); plain.requiresBlueprint.clear();
    FakeBlueprints none;
    CHECK(check(plain, [&](const std::string& k) { return none.unlocked(k); }).ok);                   // ungated recipes are unaffected
    Recipe noName = gated(); noName.blueprintName.clear();
    CHECK_EQ(check(noName, [](const std::string&) { return false; }).reason, std::string("blueprint required: missile_pack"));
}

TEST(blueprints_gate_does_not_exist_without_the_service) {
    // gameplay/blueprints disabled -> no hasBlueprint callback -> a gated recipe crafts like any other (no softlock)
    CHECK(check(gated(), nullptr).ok);
    CHECK_EQ(check(gated(), nullptr, 0).reason, std::string("missing 3 iron"));
}

TEST(blueprints_recipe_parsing) {
    Recipe a = recipeFromJson("r", parseB(R"({"name":"R","result":"x","ingredients":{"iron":1}})"));
    CHECK(a.requiresBlueprint.empty());
    Recipe b = recipeFromJson("r", parseB(R"({"result":"x","ingredients":{"iron":1},"requires_blueprint":"  missile_pack "})"));
    CHECK_EQ(b.requiresBlueprint, std::string("missile_pack"));
    for (const char* bad : {R"({"requires_blueprint":5})", R"({"requires_blueprint":""})", R"({"requires_blueprint":"   "})", R"({"requires_blueprint":null})",
                            R"({"requires_blueprint":["x"]})"})
        CHECK(recipeFromJson("r", parseB(bad)).requiresBlueprint.empty());
}

TEST(blueprints_anomaly_reward_unlocks_with_service_and_still_investigates_without) {
    world::AnomalyKind k = world::anomalyKindFromJson("cache", parseB(R"({"name":"Cache","blueprint":" missile_pack ","rewards":[{"ore":"cobalt","min":2,"max":4}]})"));
    CHECK_EQ(k.blueprint, std::string("missile_pack"));
    CHECK_EQ(k.rewards.size(), (size_t)1);            // alongside ore
    std::vector<bool> done(3, false);
    FakeBlueprints bp;
    world::BlueprintGrant g = world::investigateSite(done, 1, k, &bp);
    CHECK(done[1]); CHECK(g.unlocked); CHECK_EQ(g.id, std::string("missile_pack")); CHECK_EQ(bp.unlockCalls, 1); CHECK(bp.unlocked("missile_pack"));
    g = world::investigateSite(done, 2, k, &bp);      // a second cache: already known
    CHECK(done[2]); CHECK(!g.unlocked);
    std::vector<bool> done2(3, false);
    g = world::investigateSite(done2, 0, k, nullptr); // gameplay/blueprints off: nothing granted, the site is still done
    CHECK(done2[0]); CHECK(!g.unlocked);
    world::AnomalyKind plain = world::anomalyKindFromJson("wreck", parseB(R"({"rewards":[{"ore":"iron"}]})"));
    CHECK(plain.blueprint.empty());
    FakeBlueprints bp2;
    g = world::investigateSite(done2, 1, plain, &bp2);
    CHECK(done2[1]); CHECK(!g.unlocked); CHECK_EQ(bp2.unlockCalls, 0);   // ore-only kinds never touch the service
    CHECK(world::anomalyKindFromJson("x", parseB(R"({"blueprint":3})")).blueprint.empty());
}

TEST(blueprints_shipped_data_is_consistent) {
    engine::Json recipes = loadB("data/recipes.json"), anomalies = loadB("data/anomalies.json"), bps = loadB("data/blueprints.json");
    CHECK(recipes.isObject() && anomalies.isObject() && bps.isObject());
    std::set<std::string> gatedIds, granted;
    for (auto& id : recipes.keys()) {
        if (id.empty() || id[0] == '_') continue;
        Recipe r = recipeFromJson(id, recipes[id]);
        if (!r.requiresBlueprint.empty()) { gatedIds.insert(r.requiresBlueprint); CHECK(bps.has(r.requiresBlueprint)); }
    }
    for (const char* basic : {"warp_fuel_cell", "fuel_canister", "repair_kit", "hull_plating"})   // survival recipes are never gated
        CHECK(recipeFromJson(basic, recipes[basic]).requiresBlueprint.empty());
    for (auto& id : anomalies.keys()) {
        if (id.empty() || id[0] == '_') continue;
        auto k = world::anomalyKindFromJson(id, anomalies[id]);
        if (!k.blueprint.empty()) { granted.insert(k.blueprint); CHECK(bps.has(k.blueprint)); }
    }
    CHECK(!gatedIds.empty());
    for (auto& g : gatedIds) CHECK(granted.count(g) == 1);                // every gated recipe can actually be unlocked by exploring
}
