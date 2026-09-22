#include <cmath>
#include "core/input_methods/agent/agent_rules.h"
#include "tests/test.h"

namespace { bool close(double a, double b, double eps) { return std::fabs(a - b) < eps; } }

TEST(agent_parse_commands_reads_numbers_ignores_the_rest) {
    std::string err;
    engine::Json j = engine::Json::parse(R"({"thrust": 1.0, "yaw": -0.3, "fire": 1, "note": "ignored", "bad": true})", &err);
    auto cmds = agent::parseCommands(j);
    CHECK_EQ(cmds.size(), 5u);   // every key is present; non-numeric values just become 0 (see below)
    CHECK(cmds["thrust"] == 1.0f);
    CHECK(cmds["yaw"] == -0.3f);
    CHECK(cmds["fire"] == 1.0f);
    CHECK(cmds["note"] == 0.0f);   // a string value: quietly 0, never a crash
    CHECK(cmds["bad"] == 0.0f);    // a bool value: same
}

TEST(agent_parse_commands_non_object_or_bad_json_is_empty) {
    std::string err;
    CHECK(agent::parseCommands(engine::Json::parse("[1,2,3]", &err)).empty());
    CHECK(agent::parseCommands(engine::Json::parse("not json", &err)).empty());
    CHECK(agent::parseCommands(engine::Json()).empty());
}

TEST(agent_build_status_round_trips_through_the_real_parser) {
    agent::ShipReport ship;
    ship.alive = true; ship.hp = 80; ship.maxHp = 100; ship.shield = 20; ship.maxShield = 200;
    ship.warpFuel = 55; ship.maxWarpFuel = 100; ship.warping = false; ship.speed = 123.4f;
    ship.x = 10; ship.y = -5; ship.z = 3000; ship.vx = 1; ship.vy = 2; ship.vz = 3;
    agent::DockReport dock; dock.have = true; dock.docked = false; dock.nearestName = "Station 1"; dock.distance = 450.2f; dock.ok = false; dock.reason = "too far";
    agent::CargoReport cargo; cargo.used = 12; cargo.capacity = 400; cargo.stacks = {{"cobalt", 3}, {"crystal", 1}}; cargo.perks = {"ore_scanner"};
    std::vector<agent::AsteroidReport> rocks = {{5, "gold", 210.7, 4.5f, 100, 0, 3100}};

    engine::Json j = agent::buildStatus(42, 7.5, ship, dock, cargo, rocks);
    std::string dumped = j.dump();
    std::string err;
    engine::Json back = engine::Json::parse(dumped, &err);
    CHECK(err.empty());
    CHECK_EQ(back["frame"].num(), 42.0);
    CHECK(close(back["time"].num(), 7.5, 1e-9));
    CHECK(close(back["ship"]["hp"].num(), 80.0, 1e-6));
    CHECK(close(back["ship"]["position"].at(2).num(), 3000.0, 1e-6));
    CHECK_EQ(back["docking"]["nearestName"].str(), std::string("Station 1"));
    CHECK(back["docking"]["ok"].boolean(true) == false);
    CHECK_EQ(back["cargo"]["stacks"].size(), 2u);
    CHECK_EQ(back["cargo"]["stacks"].at(0)["id"].str(), std::string("cobalt"));
    CHECK_EQ((int)back["cargo"]["stacks"].at(0)["count"].num(), 3);
    CHECK_EQ(back["cargo"]["perks"].size(), 1u);
    CHECK_EQ(back["cargo"]["perks"].at(0).str(), std::string("ore_scanner"));
    CHECK_EQ(back["nearbyAsteroids"].size(), 1u);
    CHECK_EQ(back["nearbyAsteroids"].at(0)["ore"].str(), std::string("gold"));
    CHECK(close(back["nearbyAsteroids"].at(0)["distance"].num(), 210.7, 1e-6));
}

TEST(agent_build_status_no_docking_service_omits_the_section) {
    agent::ShipReport ship;
    agent::DockReport dock;   // .have stays false: no IDocking loaded
    agent::CargoReport cargo;
    engine::Json j = agent::buildStatus(0, 0, ship, dock, cargo, {});
    CHECK(!j["docking"].isObject());   // absent, not a half-filled object
}
