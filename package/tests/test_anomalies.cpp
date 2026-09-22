#include <cmath>
#include <fstream>
#include <sstream>
#include "world/anomalies/anomaly_rules.h"
#include "gameplay/crafting/crafting_data.h"
#include "gameplay/crafting/crafting_rules.h"
#include "tests/test.h"

namespace {
bool closeA(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b)); }
double distA(const world::Vec3d& a, const world::Vec3d& b) { return world::adetail::dist(a, b); }

world::AnomalyGenParams paramsFor(unsigned seed, const world::System& sys, bool belt) {
    world::AnomalyGenParams p;
    p.seed = seed; p.count = 9; p.sun = sys.sun;
    for (const auto& b : sys.bodies) p.bodies.push_back({b.id, b.kind, b.radius, b.orbitRadius, b.parent});
    p.stations.push_back({1, sys.bodies.size() > 1 ? sys.bodies[1].radius * 2.0 : 1000.0});
    if (belt) for (int i = 0; i < 20; i++) p.belt.push_back({sys.sun.x + 60000.0 + i * 500.0, sys.sun.y, sys.sun.z});
    p.kindWeights = {3, 2, 1, 2};
    return p;
}
world::Vec3d anchorPos(const world::System& sys, const world::AnomalySite& s) { return s.anchor >= 0 ? sys.bodies[(size_t)s.anchor].position : world::Vec3d{}; }
} // namespace

TEST(anomalies_generation_is_deterministic_and_clear_of_bodies) {
    for (unsigned seed : {1234u, 7u, 99999u}) {
        world::SystemParams sp; sp.seed = seed;
        world::System sys = world::generateSystem(sp); world::updatePositions(sys, 0.0);
        auto a = world::generateAnomalies(paramsFor(seed, sys, true)), b = world::generateAnomalies(paramsFor(seed, sys, true));
        CHECK_EQ(a.size(), (size_t)9);
        CHECK_EQ(a.size(), b.size());
        int zones[3] = {0, 0, 0};
        for (size_t i = 0; i < a.size(); i++) {
            CHECK_EQ(a[i].id, (int)i); CHECK_EQ(a[i].kind, b[i].kind); CHECK_EQ(a[i].seed, b[i].seed);
            CHECK(closeA(a[i].offset.x, b[i].offset.x) && closeA(a[i].offset.y, b[i].offset.y) && closeA(a[i].offset.z, b[i].offset.z));
            CHECK(a[i].kind >= 0 && a[i].kind < 4);
            zones[(int)a[i].zone]++;
            world::Vec3d pos = world::sitePosition(a[i], anchorPos(sys, a[i]));
            for (const auto& body : sys.bodies)                                  // best effort at t = 0: outside every body with a margin
                CHECK(distA(pos, body.position) > body.radius + 100.0);
        }
        CHECK(zones[0] > 0 && zones[1] + zones[0] > 3 && zones[2] > 0);          // deep space and belt sites both exist
    }
    world::SystemParams sp; sp.seed = 1234;
    world::System sys = world::generateSystem(sp); world::updatePositions(sys, 0.0);
    auto other = world::generateAnomalies(paramsFor(4321, sys, true)), mine = world::generateAnomalies(paramsFor(1234, sys, true));
    bool differs = false;
    for (size_t i = 0; i < mine.size(); i++) if (!closeA(mine[i].offset.x, other[i].offset.x)) differs = true;
    CHECK(differs);                                                             // another seed, another layout
    auto noBelt = world::generateAnomalies(paramsFor(1234, sys, false));
    for (auto& s : noBelt) CHECK(s.zone != world::SiteZone::Belt);
    world::AnomalyGenParams empty = paramsFor(1234, sys, false); empty.kindWeights.clear();
    CHECK(world::generateAnomalies(empty).empty());                             // no kinds = no sites, no crash
}

TEST(anomalies_near_body_sites_avoid_station_rings) {
    world::AnomalyGenParams p;
    p.bodies = {{0, world::BodyKind::Sun, 1000, 0, -1}, {1, world::BodyKind::Planet, 500, 40000, 0}};
    p.stations = {{1, 1200}};
    for (unsigned s = 1; s < 50; s++) {
        world::detail::Rng rng(s);
        double d = world::nearBodyDistance(p, p.bodies[1], rng);
        if (d < 0) continue;
        CHECK(d >= 500 + world::kAnomalyBodyMargin);
        CHECK(std::fabs(d - 1200) >= world::kAnomalyStationMargin);
    }
}

TEST(anomalies_detect_and_investigate_ranges) {
    CHECK(closeA(world::anomalyDetectRange(4000, 1.5f), 6000));
    CHECK(closeA(world::anomalyDetectRange(4000, 99), 20000));                  // multiplier clamped to 5
    CHECK(world::anomalyDetected(5900, 4000, 1.5f, true, false));
    CHECK(!world::anomalyDetected(6100, 4000, 1.5f, true, false));
    CHECK(!world::anomalyDetected(100, 4000, 1.0f, false, false));              // no perk: inert
    CHECK(!world::anomalyDetected(100, 4000, 1.0f, true, true));                // investigated: gone
    CHECK(!world::anomalyDetected(std::nan(""), 4000, 1.0f, true, false));
    CHECK(world::anomalyInvestigates(74, 75, true, false));
    CHECK(!world::anomalyInvestigates(76, 75, true, false));
    CHECK(!world::anomalyInvestigates(10, 75, false, false));
    CHECK(!world::anomalyInvestigates(10, 75, true, true));                     // one time only
}

TEST(anomalies_reward_is_deterministic_and_in_range) {
    world::AnomalyKind k;
    k.messages = {"a", "b", "c"};
    k.rewards = {{"gold", 5, 10, 1}, {"crystal", 2, 4, 1}, {"iron", 1, 1, 0}};
    for (uint32_t s = 0; s < 200; s++) {
        world::AnomalyRoll r = world::rollAnomaly(k, s), again = world::rollAnomaly(k, s);
        CHECK_EQ(r.ore, again.ore); CHECK_EQ(r.amount, again.amount); CHECK_EQ(r.message, again.message);
        CHECK(r.message >= 0 && r.message < 3);
        CHECK(r.ore != "iron");                                                 // weight 0 is never picked
        if (r.ore == "gold") CHECK(r.amount >= 5 && r.amount <= 10);
        else { CHECK_EQ(r.ore, std::string("crystal")); CHECK(r.amount >= 2 && r.amount <= 4); }
    }
    world::AnomalyKind none;                                                     // no rewards: no ore, no crash
    CHECK(world::rollAnomaly(none, 5).ore.empty());
}

TEST(anomalies_investigated_ids_round_trip) {
    std::vector<bool> done = {false, true, false, true, true};
    std::vector<int> ids = world::investigatedIds(done);
    CHECK_EQ(ids.size(), (size_t)3);
    CHECK(world::investigatedMask(ids, 5) == done);
    auto m = world::investigatedMask({-1, 2, 2, 99}, 4);                        // bad ids ignored, duplicates harmless
    CHECK(m == (std::vector<bool>{false, false, true, false}));
    CHECK(world::investigatedMask({}, 0).empty());
}

TEST(anomalies_data_parsing_defaults_and_sanitising) {
    std::string err;
    engine::Json j = engine::Json::parse(R"({"name":"Wreck","messages":["x",""],"rewards":[{"ore":"gold","min":-3,"max":-9},{"min":4},{"ore":"iron","min":5,"max":2,"weight":-1}],"detect_mult":50,"weight":-2})", &err);
    world::AnomalyKind k = world::anomalyKindFromJson("wreck", j);
    CHECK_EQ(k.name, std::string("Wreck"));
    CHECK_EQ(k.messages.size(), (size_t)1);                                     // empty text dropped
    CHECK_EQ(k.rewards.size(), (size_t)2);                                      // entry without an ore dropped
    CHECK(k.rewards[0].min >= 1 && k.rewards[0].max >= k.rewards[0].min);
    CHECK_EQ(k.rewards[1].max, 5); CHECK(closeA(k.rewards[1].weight, 0));
    CHECK(closeA(k.detectMul, 5.0)); CHECK(closeA(k.weight, 0));
    world::AnomalyKind e = world::anomalyKindFromJson("empty", engine::Json::object());
    CHECK_EQ(e.name, std::string("empty")); CHECK_EQ(e.messages.size(), (size_t)1);
    CHECK(e.rewards.empty()); CHECK(closeA(e.detectMul, 1.0)); CHECK(closeA(e.weight, 1.0));
}

TEST(anomalies_shipped_data_file_is_valid) {
    std::ifstream f("data/anomalies.json"), o("data/ores.json");
    CHECK(f.good() && o.good());
    std::stringstream ss, os; ss << f.rdbuf(); os << o.rdbuf();
    std::string err;
    engine::Json j = engine::Json::parse(ss.str(), &err), ores = engine::Json::parse(os.str(), &err);
    int kinds = 0;
    for (const auto& id : j.keys()) {
        if (id.empty() || id[0] == '_') continue;
        kinds++;
        world::AnomalyKind k = world::anomalyKindFromJson(id, j[id]);
        CHECK(!k.rewards.empty());
        for (const auto& r : k.rewards) CHECK(ores.has(r.ore));                 // every reward is a real ore
        CHECK(k.messages.size() >= 2);
    }
    CHECK(kinds >= 2 && kinds <= 5);                                  // 5: + ancient_blueprint_cache (docs/BLUEPRINTS.md)
}

TEST(anomalies_scanner_item_sets_the_perk_once) {
    std::string err;
    engine::Json item = engine::Json::parse(R"({"permanent":true,"effect":{"anomaly_scan":true}})", &err);
    gameplay::Effect e = gameplay::effectFromJson(item);
    CHECK(e.anomalyScan && e.any());
    gameplay::ShipState s;
    gameplay::UsePlan p = gameplay::decideUse(e, s);
    CHECK(p.ok && p.setAnomalyScanner && !p.setOreScanner);
    s.anomalyScanner = true; p = gameplay::decideUse(e, s);
    CHECK(!p.ok); CHECK_EQ(p.reason, std::string("anomaly scanner already installed"));
}
