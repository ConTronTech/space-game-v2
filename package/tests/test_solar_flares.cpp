#include <cmath>
#include "world/solar_flares/flare_rules.h"
#include "tests/test.h"

namespace {
bool closeF(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b)); }
world::FlareParams smallFlare() {
    world::FlareParams p;
    p.intervalMin = 10; p.intervalMax = 20; p.warningSeconds = 5;
    p.shellSpeed = 1000; p.shellThickness = 200; p.startRadius = 1000; p.maxRange = 11000;
    p.damageAtSun = 100; p.damageMinRange = 1000; p.damageFalloffRange = 11000;
    return p;
}
} // namespace

TEST(solar_flares_interval_is_seeded_and_in_range) {
    for (uint32_t seed : {1u, 1234u, 0xFFFFFFFFu})
        for (uint32_t cycle = 0; cycle < 50; cycle++) {
            double a = world::flareInterval(seed, cycle, 90, 240), b = world::flareInterval(seed, cycle, 90, 240);
            CHECK_EQ(a, b);
            CHECK(a >= 90 && a < 240);
        }
    CHECK(world::flareInterval(1234, 0, 90, 240) != world::flareInterval(1234, 1, 90, 240));   // re-rolled per cycle, not a metronome
    CHECK(world::flareInterval(1234, 0, 90, 240) != world::flareInterval(4321, 0, 90, 240));
    CHECK_EQ(world::flareInterval(7, 3, 50, 50), 50.0);
    double s = world::flareInterval(7, 3, 240, 90);                                            // swapped bounds still land in range
    CHECK(s >= 90 && s < 240);
}

TEST(solar_flares_warning_countdown) {
    CHECK_EQ(world::flareWarningEta(20, 15), -1.0);
    CHECK_EQ(world::flareWarningEta(15, 15), 15.0);
    CHECK_EQ(world::flareWarningEta(3.5, 15), 3.5);
    CHECK_EQ(world::flareWarningEta(0, 15), -1.0);
    CHECK_EQ(world::flareWarningEta(-1, 15), -1.0);
    CHECK_EQ(world::flareWarningEta(NAN, 15), -1.0);
}

TEST(solar_flares_shell_radius_and_swept_hit) {
    CHECK_EQ(world::flareShellRadius(0, 1000, 800), 800.0);
    CHECK_EQ(world::flareShellRadius(2.5, 1000, 800), 3300.0);
    CHECK_EQ(world::flareShellRadius(-5, 1000, 800), 800.0);
    CHECK(world::flareShellHits(5000, 4990, 5010, 100));        // inside the sweep
    CHECK(world::flareShellHits(5040, 4900, 5000, 100));        // within half the thickness ahead of the front
    CHECK(!world::flareShellHits(5060, 4900, 5000, 100));
    CHECK(!world::flareShellHits(4840, 4900, 5000, 100));       // already behind the front
    CHECK(world::flareShellHits(7000, 1000, 50000, 10));        // a huge step never jumps over the ship
    CHECK(!world::flareShellHits(NAN, 1000, 50000, 10));
}

TEST(solar_flares_damage_falloff) {
    world::FlareParams p = smallFlare();                        // full inside 1000, 0 at 11000
    CHECK_EQ(world::flareDamage(0, p), 100.0f);
    CHECK_EQ(world::flareDamage(1000, p), 100.0f);
    CHECK(closeF(world::flareDamage(6000, p), 50.0));           // halfway: the linear missile-blast shape
    CHECK(closeF(world::flareDamage(8500, p), 25.0));
    CHECK_EQ(world::flareDamage(11000, p), 0.0f);
    CHECK_EQ(world::flareDamage(20000, p), 0.0f);
    CHECK_EQ(world::flareDamage(NAN, p), 0.0f);
    CHECK(world::flareDamage(3000, p) > world::flareDamage(4000, p));   // closer to the sun is worse
    p.damageAtSun = 0;
    CHECK_EQ(world::flareDamage(0, p), 0.0f);
}

// A huge maxRange (the front travels far past the outer planets before the flare ends) must NOT stretch the damage
// falloff out with it - damageFalloffRange is independent, so a body well inside maxRange but past the falloff range
// takes zero damage. This is the fix for the overnight build's flagged issue: damage barely dropping at the planets.
TEST(solar_flares_damage_falloff_is_independent_of_max_range) {
    world::FlareParams p = smallFlare();
    p.maxRange = 900000;               // far: the front keeps travelling long after damage has faded out
    p.damageFalloffRange = 11000;      // short: damage is done well before the front reaches the edge of the system
    CHECK_EQ(world::flareDamage(11000, p), 0.0f);
    CHECK_EQ(world::flareDamage(50000, p), 0.0f);
    CHECK(closeF(world::flareDamage(6000, p), 50.0));   // still the same falloff shape inside the (short) falloff range
}

TEST(solar_flares_sanitize_params) {
    world::FlareParams p;
    p.intervalMin = -5; p.intervalMax = 0; p.shellSpeed = 0; p.shellThickness = NAN; p.maxRange = 10; p.startRadius = 500; p.damageAtSun = -3;
    world::FlareParams s = world::sanitizeFlareParams(p);
    CHECK(s.intervalMin >= 1 && s.intervalMax >= s.intervalMin);
    CHECK(s.shellSpeed >= 1);
    CHECK_EQ(s.shellThickness, world::FlareParams{}.shellThickness);
    CHECK(s.maxRange > s.startRadius);
    CHECK_EQ(s.damageAtSun, 0.0f);
    CHECK(world::autoFlareMaxRange(300000, 1500) == 900000.0);
    CHECK(world::autoFlareMaxRange(0, 1000) > 1000.0);                // no planets: still a real range
}

TEST(solar_flares_full_cycle_emits_warning_eruption_end_in_order) {
    const world::FlareParams p = smallFlare();
    world::FlareState s = world::startFlareCycle(42, p);
    const double firstGap = s.untilEruption;
    CHECK(firstGap >= 10 && firstGap < 20);
    const double dt = 1.0 / 60.0;
    int warnings = 0, erupted = 0, ended = 0, sweepSteps = 0;
    double t = 0, eruptAt = -1, endAt = -1, lastEta = 1e9, firstEta = -1;
    bool shipHit = false;
    const double shipDist = 5000;
    for (int i = 0; i < 60 * 60 && ended == 0; i++) {
        world::FlareStep st = world::stepFlare(s, p, dt);
        t += dt;
        if (st.warningEta >= 0) {
            warnings++;
            if (firstEta < 0) firstEta = st.warningEta;
            CHECK(st.warningEta < lastEta);                       // counts down
            lastEta = st.warningEta;
            CHECK(erupted == 0);
        }
        if (st.erupted) { erupted++; eruptAt = t; CHECK(!shipHit); }
        if (st.radius >= 0) {
            sweepSteps++;
            CHECK(st.prevRadius >= 0 && st.radius >= st.prevRadius);
            if (world::flareShellHits(shipDist, st.prevRadius, st.radius, p.shellThickness)) shipHit = true;
        }
        if (st.ended) { ended++; endAt = t; }
    }
    CHECK_EQ(erupted, 1);
    CHECK_EQ(ended, 1);
    CHECK(shipHit);
    CHECK(closeF(eruptAt, firstGap, 0.01));
    CHECK(firstEta <= p.warningSeconds && firstEta > p.warningSeconds - 0.1);
    CHECK(warnings > 60 * 4 && warnings <= 60 * 5 + 1);           // ~every step of the 5 s window
    // front from 1000 to past 11000 + 100 at 1000 u/s: ~10.1 s
    CHECK(closeF(endAt - eruptAt, 10.1, 0.01));
    CHECK(sweepSteps > 0);
    // back to waiting with a fresh roll for cycle 1
    CHECK(!s.erupting);
    CHECK_EQ(s.cycle, 1u);
    CHECK_EQ(s.radius, -1.0);
    CHECK(closeF(s.untilEruption, world::flareInterval(42, 1, p.intervalMin, p.intervalMax)));
}

TEST(solar_flares_big_step_still_erupts_and_sweeps) {
    world::FlareParams p = smallFlare();
    world::FlareState s = world::startFlareCycle(9, p);
    world::FlareStep st = world::stepFlare(s, p, s.untilEruption + 2.0);   // overshoot counts as flare time
    CHECK(st.erupted);
    CHECK_EQ(st.prevRadius, p.startRadius);
    CHECK(closeF(st.radius, p.startRadius + 2.0 * p.shellSpeed));
    CHECK(world::flareShellHits(2000, st.prevRadius, st.radius, p.shellThickness));
    world::FlareStep z = world::stepFlare(s, p, 0);                       // dt 0 (or NaN) changes nothing
    CHECK(!z.erupted && !z.ended);
    CHECK_EQ(z.radius, st.radius);
}
