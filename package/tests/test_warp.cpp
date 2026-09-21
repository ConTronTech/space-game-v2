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
    auto r = warp::step(true, fuel, {1900, 0, 0}, kFwd, 1.0f, p);
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
