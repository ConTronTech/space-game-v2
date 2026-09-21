#include <fstream>
#include <sstream>
#include "engine/json.h"
#include "gameplay/inventory/inventory_rules.h"
#include "tests/test.h"

namespace {
bool close(double a, double b, double tol = 1e-4) { return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b)); }
} // namespace

TEST(inventory_add_never_exceeds_capacity_and_accepts_partially) {
    gameplay::Cargo c; c.setCapacity(100);
    CHECK_EQ(c.add("iron", 60), 60); CHECK(close(c.used(), 60)); CHECK(close(c.free(), 40));
    CHECK_EQ(c.add("copper", 60), 40);                                   // partial: only 40 fit
    CHECK(close(c.used(), 100)); CHECK(close(c.free(), 0));
    CHECK_EQ(c.add("iron", 1), 0);                                       // full: nothing
    CHECK_EQ(c.count("copper"), 40); CHECK_EQ(c.count("iron"), 60);
    CHECK_EQ(c.add("gold", 1000000), 0);                                 // absurd amount into a full hold
    gameplay::Cargo big; big.setCapacity(100);
    CHECK_EQ(big.add("iron", 2000000000), 100);                          // huge request: clamped to capacity, no overflow
    CHECK(close(big.used(), 100));
}

TEST(inventory_bad_amounts_and_ids_are_harmless) {
    gameplay::Cargo c; c.setCapacity(50);
    CHECK_EQ(c.add("iron", 0), 0); CHECK_EQ(c.add("iron", -5), 0); CHECK_EQ(c.add("", 10), 0);
    CHECK_EQ(c.stacks().size(), (size_t)0);                              // nothing created
    CHECK(!c.remove("iron", 0)); CHECK(!c.remove("iron", -3)); CHECK(!c.remove("nothing", 1));
    CHECK_EQ(c.add("mystery_thing", 5), 5);                              // an id nobody defined is just a raw stack
    CHECK_EQ(c.count("mystery_thing"), 5);
    CHECK_EQ(c.count("iron"), 0); CHECK_EQ(c.count(""), 0);
    c.setCapacity(-10); CHECK(close(c.capacity(), 0)); CHECK(close(c.free(), 0));   // a negative capacity is a zero-size hold
    CHECK_EQ(c.add("iron", 1), 0);
}

TEST(inventory_remove_is_atomic) {
    gameplay::Cargo c; c.setCapacity(100);
    c.add("iron", 30);
    CHECK(!c.remove("iron", 31)); CHECK_EQ(c.count("iron"), 30);         // not enough: nothing removed
    CHECK(c.remove("iron", 10)); CHECK_EQ(c.count("iron"), 20); CHECK(close(c.used(), 20));
    CHECK(c.remove("iron", 20)); CHECK_EQ(c.count("iron"), 0);
    CHECK_EQ(c.stacks().size(), (size_t)0);                              // an emptied stack disappears
    CHECK(!c.remove("iron", 1));
    CHECK(close(c.free(), 100));                                         // and the space is free again
}

TEST(inventory_stacks_keep_a_stable_order) {
    gameplay::Cargo c; c.setCapacity(1000);
    c.add("iron", 5); c.add("gold", 1); c.add("copper", 3); c.add("iron", 2);    // adding to an existing stack keeps its place
    auto s = c.stacks();
    CHECK_EQ(s.size(), (size_t)3);
    CHECK_EQ(s[0].id, std::string("iron")); CHECK_EQ(s[0].amount, 7);
    CHECK_EQ(s[1].id, std::string("gold")); CHECK_EQ(s[2].id, std::string("copper"));
    c.remove("gold", 1);                                                 // removing a stack keeps the others in order
    s = c.stacks(); CHECK_EQ(s.size(), (size_t)2); CHECK_EQ(s[0].id, std::string("iron")); CHECK_EQ(s[1].id, std::string("copper"));
}

TEST(inventory_item_volume_takes_more_room) {
    gameplay::Cargo c; c.setCapacity(10);
    c.setVolume("engine_part", 4.0f); c.setVolume("feather", 0.0f);
    CHECK_EQ(c.add("engine_part", 5), 2);                                // 4 units each: only 2 fit in 10
    CHECK(close(c.used(), 8)); CHECK(close(c.free(), 2));
    CHECK_EQ(c.add("iron", 5), 2);                                       // ore 1 unit each: 2 left
    CHECK_EQ(c.add("feather", 500), 500);                                // zero volume always fits
    CHECK(close(c.volumeOf("iron"), 1.0)); CHECK(close(c.volumeOf("engine_part"), 4.0));
    c.setVolume("bad", -3.0f); CHECK(close(c.volumeOf("bad"), 0.0));     // negative volume is clamped
}

TEST(inventory_assign_from_a_save_drops_bad_entries_and_keeps_overfull_cargo) {
    gameplay::Cargo c; c.setCapacity(100);
    c.assign({{"iron", 30}, {"", 5}, {"gold", 0}, {"copper", -4}, {"iron", 10}, {"cobalt", 2}});
    CHECK_EQ(c.stacks().size(), (size_t)2);                              // iron (merged 30+10) and cobalt
    CHECK_EQ(c.count("iron"), 40); CHECK_EQ(c.count("cobalt"), 2);
    c.assign({{"iron", 500}});                                           // saved with a bigger hold: kept, but nothing more fits
    CHECK_EQ(c.count("iron"), 500); CHECK(close(c.free(), 0)); CHECK_EQ(c.add("gold", 1), 0);
    c.assign({}); CHECK(close(c.used(), 0));
}

TEST(inventory_capacity_levels) {
    auto lv = gameplay::defaultCargoLevels();
    CHECK_EQ((int)lv.size(), 4);
    const float caps[4] = {100, 200, 400, 800};
    for (int i = 0; i < 4; i++) CHECK(close(100.0f * lv[i].capacityMult, caps[i]));
    CHECK_EQ(gameplay::clampLevel(-2, 4), 0); CHECK_EQ(gameplay::clampLevel(2, 4), 2); CHECK_EQ(gameplay::clampLevel(50, 4), 3); CHECK_EQ(gameplay::clampLevel(1, 0), 0);
    gameplay::Cargo c; c.setCapacity(100 * lv[0].capacityMult); c.add("iron", 100);
    CHECK_EQ(c.add("iron", 1), 0);
    c.setCapacity(100 * lv[2].capacityMult);                             // an upgrade makes room at once
    CHECK_EQ(c.add("iron", 1000), 300); CHECK_EQ(c.count("iron"), 400);
}

TEST(inventory_give_flag_parsing) {
    auto g = gameplay::parseGive("iron:50,copper:20");
    CHECK_EQ(g.size(), (size_t)2); CHECK_EQ(g[0].id, std::string("iron")); CHECK_EQ(g[0].amount, 50); CHECK_EQ(g[1].id, std::string("copper")); CHECK_EQ(g[1].amount, 20);
    CHECK_EQ(gameplay::parseGive("").size(), (size_t)0);
    auto b = gameplay::parseGive("iron:abc,:5,gold:-3,,cobalt:7,lonely");
    CHECK_EQ(b.size(), (size_t)2);                                       // bad pieces skipped: cobalt:7 and 'lonely' (= 1)
    CHECK_EQ(b[0].id, std::string("cobalt")); CHECK_EQ(b[1].id, std::string("lonely")); CHECK_EQ(b[1].amount, 1);
}

TEST(inventory_shipped_cargo_levels_file_is_valid) {
    std::ifstream f("data/cargo.json");                                  // tests run from the repo root
    CHECK(f.good());
    std::stringstream ss; ss << f.rdbuf();
    std::string err;
    engine::Json j = engine::Json::parse(ss.str(), &err);
    CHECK(j.isObject());
    float last = 0; int n = 0;
    for (const char* id : {"level0", "level1", "level2", "level3"}) {
        const engine::Json& l = j[id];
        CHECK(l.isObject());
        float m = (float)l["capacity_mult"].num(0);
        CHECK(m > 0); CHECK(m > last); last = m; n++;
    }
    CHECK_EQ(n, 4); CHECK(close(j["level0"]["capacity_mult"].num(0), 1.0)); CHECK(close(j["level3"]["capacity_mult"].num(0), 8.0));
}

TEST(inventory_save_format_round_trip_through_json) {
    // the module saves an array of {id, amount} in order plus the level; this checks the same shape reads back through Cargo::assign
    gameplay::Cargo a; a.setCapacity(500); a.add("iron", 12); a.add("copper", 7); a.add("unknown_x", 3);
    engine::Json arr = engine::Json::array();
    for (auto& s : a.stacks()) arr.push(engine::Json::object().set("id", s.id).set("amount", s.amount));
    engine::Json saved = engine::Json::object().set("level", 2).set("stacks", arr);
    std::string text = saved.dump();
    std::string err; engine::Json back = engine::Json::parse(text, &err);
    std::vector<gameplay::Stack> in;
    for (size_t i = 0; i < back["stacks"].size(); i++) in.push_back({back["stacks"].at(i)["id"].str(), (int)back["stacks"].at(i)["amount"].num(0)});
    gameplay::Cargo b; b.setCapacity(500); b.assign(in);
    CHECK_EQ(b.stacks().size(), (size_t)3);
    for (int i = 0; i < 3; i++) { CHECK_EQ(b.stacks()[i].id, a.stacks()[i].id); CHECK_EQ(b.stacks()[i].amount, a.stacks()[i].amount); }
    CHECK_EQ((int)back["level"].num(0), 2);
    // an old save without the keys: empty stacks, level kept
    engine::Json empty = engine::Json::parse("{}", &err);
    CHECK_EQ(empty["stacks"].size(), (size_t)0);
}
