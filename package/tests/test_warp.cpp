#include <fstream>
#include <sstream>
#include "engine/json.h"
#include "ship/warp_drive/warp_rules.h"
#include "tests/test.h"

namespace {
using warp::V3;
bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
bool nearRel(float a, float b) { return std::fabs(a - b) < 1e-4f * std::max(1.0f, std::fabs(b)); }
const V3 kFwd{0, 0, -1};
} // namespace

TEST(warp_engage_refused_when_dead_empty_or_already_on) {
    warp::Params p;
    CHECK(warp::canEngage(false, true, 100, p));
    CHECK(!warp::canEngage(false, false, 100, p));      // dead
    CHECK(!warp::canEngage(false, true, 0.5f, p));      // below min_fuel
    CHECK(!warp::canEngage(false, true, 0, p));
    CHECK(!warp::canEngage(true, true, 100, p));        // already engaged
    CHECK(warp::canEngage(false, true, 1.0f, p));       // exactly min_fuel is enough
}

TEST(warp_accelerates_to_cap_and_never_exceeds_it) {
    warp::Params p;
    V3 v{0, 0, 0};
    float fuel = 1e6f;
    for (int i = 0; i < 600; i++) {
        auto r = warp::step(true, fuel, v, kFwd, 1.0f / 60, p);
        v = r.velocity;
        CHECK(warp::length(v) <= p.speed + 1e-2f);
    }
    CHECK(nearRel(warp::length(v), p.speed));
    CHECK(v.z < 0 && near(v.x, 0));                      // along forward
    // sideways momentum is clamped too (total speed, not just forward)
    auto r = warp::step(true, fuel, {p.speed * 0.95f, 0, 0}, kFwd, 1.0f, p);
    CHECK(nearRel(warp::length(r.velocity), p.speed));
}

TEST(warp_fuel_burn_matches_drain_times_time) {
    warp::Params p;
    float fuel = 100, burned = 0;
    for (int i = 0; i < 120; i++) {                      // 2 s
        auto r = warp::step(true, fuel, {}, kFwd, 1.0f / 60, p);
        CHECK(!r.disengage);
        fuel -= r.fuelToBurn; burned += r.fuelToBurn;
    }
    CHECK(near(burned, p.fuelDrain * 2.0f));
}

TEST(warp_disengages_when_fuel_cannot_cover_a_step) {
    warp::Params p;
    float dt = 1.0f / 60, burn = p.fuelDrain * dt;
    auto ok = warp::step(true, burn * 2.0f, {}, kFwd, dt, p);
    CHECK(!ok.disengage); CHECK(near(ok.fuelToBurn, burn));
    auto low = warp::step(true, burn * 0.5f, {}, kFwd, dt, p);
    CHECK(low.disengage); CHECK(near(low.fuelToBurn, burn * 0.5f));   // burns what is left, never more
    auto none = warp::step(true, 0.0f, {}, kFwd, dt, p);
    CHECK(none.disengage); CHECK(near(none.fuelToBurn, 0));
}

TEST(warp_full_tank_lasts_about_thirty_seconds) {
    warp::Params p;
    float fuel = 100; int steps = 0;
    for (; steps < 100000; steps++) {
        auto r = warp::step(true, fuel, {}, kFwd, 1.0f / 60, p);
        fuel -= r.fuelToBurn;
        if (r.disengage) break;
    }
    CHECK(near(fuel, 0));
    CHECK(steps / 60.0f > 29.5f && steps / 60.0f < 30.5f);
}

TEST(warp_dead_ship_disengages_without_touching_anything) {
    warp::Params p;
    auto r = warp::step(false, 100, {1, 2, 3}, kFwd, 1.0f / 60, p);
    CHECK(r.disengage); CHECK(near(r.fuelToBurn, 0));
    CHECK(near(r.velocity.x, 1) && near(r.velocity.y, 2) && near(r.velocity.z, 3));
}

TEST(warp_zero_dt_changes_nothing) {
    warp::Params p;
    auto r = warp::step(true, 100, {4, 5, 6}, kFwd, 0.0f, p);
    CHECK(!r.disengage); CHECK(near(r.fuelToBurn, 0));
    CHECK(near(r.velocity.x, 4) && near(r.velocity.y, 5) && near(r.velocity.z, 6));
}

TEST(warp_exit_clamps_speed_keeping_direction) {
    warp::Params p;                                      // exit 200
    auto e = warp::exitVelocity({0, 1200, 1600}, p);     // 2000 m/s
    CHECK(nearRel(warp::length(e), 200));
    CHECK(near(e.y / warp::length(e), 0.6f)); CHECK(near(e.z / warp::length(e), 0.8f));
    auto slow = warp::exitVelocity({30, 0, 0}, p);       // already below: untouched
    CHECK(near(slow.x, 30));
}

TEST(warp_exit_speed_zero_keeps_momentum) {
    warp::Params p; p.exitSpeed = 0;
    auto e = warp::exitVelocity({0, 1200, 1600}, p);
    CHECK(near(e.y, 1200)); CHECK(near(e.z, 1600));
}

// ---- upgrade levels ----
TEST(warp_defaults_are_5_km_per_s_reached_in_2_5_seconds) {
    warp::Params p;
    CHECK(near(p.speed, 5000.0f));
    CHECK(near(warp::spoolSeconds(p), 2.5f));
    // simulate from rest: full speed after 2.5 s (150 steps), not before
    V3 v{0, 0, 0}; int steps = 0;
    while (warp::length(v) < p.speed - 1.0f && steps < 10000) { v = warp::step(true, 1e6f, v, kFwd, 1.0f / 60, p).velocity; steps++; }
    CHECK(std::fabs(steps / 60.0f - 2.5f) < 0.05f);
}

TEST(warp_level_table_matches_the_documented_speeds) {
    auto lv = warp::defaultLevels();
    CHECK_EQ((int)lv.size(), 4);
    warp::Params base;
    const float speeds[4] = {5000, 7500, 10000, 15000};
    float lastEff = 0;
    for (int i = 0; i < 4; i++) {
        warp::Params p = warp::effective(base, lv[i]);
        CHECK(near(p.speed, speeds[i]));
        CHECK(p.fuelDrain <= base.fuelDrain + 1e-6f);                       // efficiency never makes it worse
        CHECK(base.fuelDrain / p.fuelDrain > lastEff - 1e-6f);              // and rises with the level
        lastEff = base.fuelDrain / p.fuelDrain;
        CHECK(near(p.exitSpeed, base.exitSpeed)); CHECK(near(p.minFuel, base.minFuel));   // untouched
        CHECK(warp::tankRange(100, p) > (i ? warp::tankRange(100, warp::effective(base, lv[i - 1])) : 0.0f));   // a better drive goes farther
    }
    CHECK_EQ(warp::clampLevel(-3, 4), 0); CHECK_EQ(warp::clampLevel(2, 4), 2); CHECK_EQ(warp::clampLevel(99, 4), 3); CHECK_EQ(warp::clampLevel(5, 0), 0);
    warp::LevelDef bad{0.0f, -1.0f, 0.0f};                                  // nonsense in the data must not freeze the ship or divide by zero
    warp::Params b = warp::effective(base, bad);
    CHECK(b.speed > 0 && b.accelFactor > 0 && b.fuelDrain > 0 && b.fuelDrain == b.fuelDrain);
}

TEST(warp_tank_range_at_5_km_per_s) {
    warp::Params p;
    // 100 fuel at 3.33/s = 30.03 s; 2.5 s of ramp covers 6,250 m, the rest is cruising at 5 km/s
    float total = 100.0f / p.fuelDrain;
    CHECK(std::fabs(warp::tankRange(100, p) - (6250.0f + 5000.0f * (total - 2.5f))) < 1.0f);
    CHECK(warp::tankRange(100, p) > 140000.0f && warp::tankRange(100, p) < 150000.0f);
    CHECK(near(warp::tankRange(0, p), 0.0f));
    CHECK(warp::tankRange(1, p) < 6250.0f);                                 // a nearly empty tank never gets out of the ramp
    // simulated range agrees with the formula
    V3 v{0, 0, 0}; float fuel = 100, dist = 0;
    for (int i = 0; i < 100000; i++) {
        auto r = warp::step(true, fuel, v, kFwd, 1.0f / 60, p);
        v = r.velocity; fuel -= r.fuelToBurn; dist += warp::length(v) / 60;
        if (r.disengage) break;
    }
    CHECK(std::fabs(dist - warp::tankRange(100, p)) < 0.02f * warp::tankRange(100, p));
}

TEST(warp_shipped_level_table_is_valid) {
    std::ifstream f("data/warp_drive.json");                                // tests run from the repo root
    CHECK(f.good());
    std::stringstream ss; ss << f.rdbuf();
    std::string err;
    engine::Json j = engine::Json::parse(ss.str(), &err);
    CHECK(j.isObject());
    int n = 0; float lastSpeed = 0, lastEff = 0;
    for (const char* id : {"level0", "level1", "level2", "level3"}) {
        const engine::Json& l = j[id];
        CHECK(l.isObject());
        float s = (float)l["speed_mult"].num(0), a = (float)l["accel_mult"].num(0), e = (float)l["fuel_efficiency"].num(0);
        CHECK(s > 0 && a > 0 && e > 0);
        CHECK(s > lastSpeed - 1e-6f); CHECK(e >= lastEff - 1e-6f);          // rising
        lastSpeed = s; lastEff = e; n++;
    }
    CHECK_EQ(n, 4);
    CHECK(near((float)j["level0"]["speed_mult"].num(0), 1.0f));             // level 0 is the base drive
    CHECK(near((float)j["level3"]["speed_mult"].num(0), 3.0f));
}
