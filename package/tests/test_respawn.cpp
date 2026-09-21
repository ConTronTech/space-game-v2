#include <cmath>
#include "ship/respawn/respawn_rules.h"
#include "tests/test.h"

using ship::RespawnRules;

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

TEST(respawn_alive_ship_stays_idle) {
    RespawnRules r; r.configure(3, true);
    for (int i = 0; i < 100; i++) CHECK(!r.update(true, 0.1f, false));
    CHECK(!r.counting());
    CHECK(near(r.secondsLeft(), 0));
}

TEST(respawn_death_starts_countdown_and_fires_exactly_once) {
    RespawnRules r; r.configure(3, true);
    CHECK(!r.update(false, 0.016f, false));          // the frame it notices: counting starts
    CHECK(r.counting());
    CHECK(near(r.secondsLeft(), 3));
    int fired = 0;
    for (int i = 0; i < 500; i++) fired += r.update(false, 0.016f, false) ? 1 : 0;   // 8 s of staying dead
    CHECK_EQ(fired, 1);
    CHECK(!r.counting());
}

TEST(respawn_fires_after_the_configured_time) {
    RespawnRules r; r.configure(2, true);
    r.update(false, 0, false);
    CHECK(!r.update(false, 1.9f, false));
    CHECK(near(r.secondsLeft(), 0.1f));
    CHECK(r.update(false, 0.2f, false));
}

TEST(respawn_paused_time_does_not_advance) {
    RespawnRules r; r.configure(3, true);
    r.update(false, 0, false);
    for (int i = 0; i < 100; i++) CHECK(!r.update(false, 1.0f, true));
    CHECK(near(r.secondsLeft(), 3));
    CHECK(!r.update(false, 1.0f, false));
    CHECK(near(r.secondsLeft(), 2));
}

TEST(respawn_alive_again_cancels_the_countdown) {
    RespawnRules r; r.configure(3, true);
    r.update(false, 0, false);
    r.update(false, 1.0f, false);
    CHECK(r.counting());
    CHECK(!r.update(true, 0.016f, false));            // e.g. a save was loaded
    CHECK(!r.counting());
    CHECK(near(r.secondsLeft(), 0));
}

TEST(respawn_second_death_counts_down_again) {
    RespawnRules r; r.configure(1, true);
    r.update(false, 0, false);
    CHECK(r.update(false, 1.1f, false));
    r.update(true, 0.016f, false);                    // respawned
    CHECK(!r.update(false, 0.016f, false));           // dies again
    CHECK(r.counting());
    CHECK(near(r.secondsLeft(), 1));
    CHECK(r.update(false, 1.1f, false));
}

TEST(respawn_seconds_have_a_minimum) {
    RespawnRules r; r.configure(0.0f, true);
    CHECK(near(r.seconds(), 0.5f));
    r.configure(-4.0f, true);
    CHECK(near(r.seconds(), 0.5f));
    r.update(false, 0, false);
    CHECK(near(r.secondsLeft(), 0.5f));
}

TEST(respawn_disabled_never_counts) {
    RespawnRules r; r.configure(1, false);
    for (int i = 0; i < 200; i++) CHECK(!r.update(false, 0.1f, false));
    CHECK(!r.counting());
    CHECK(near(r.secondsLeft(), 0));
}

TEST(respawn_that_fails_to_revive_does_not_retrigger) {
    RespawnRules r; r.configure(1, true);
    r.update(false, 0, false);
    CHECK(r.update(false, 1.1f, false));
    for (int i = 0; i < 50; i++) CHECK(!r.update(false, 0.1f, false));   // still dead: idle, no loop
}
