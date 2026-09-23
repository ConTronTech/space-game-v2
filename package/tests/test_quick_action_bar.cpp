#include <cmath>
#include "tests/test.h"
#include "ui/quick_action_bar/quick_bar_rules.h"

namespace {
using quickbar::Entry;
using quickbar::Input;
using quickbar::Kind;
using quickbar::Outcome;
using quickbar::Settings;
using quickbar::State;

std::vector<Entry> threeEntries() {
    return {
        {"weapon_1", "BLASTER", "", Kind::Instant, true, true, 0},
        {"warp", "WARP", "", Kind::Toggle, true, false, 0.5f},
        {"orbit_lock", "ORBIT LOCK", "no body in range", Kind::Toggle, false, false, 0.5f},
    };
}
Input openIn() { Input i; i.openPressed = true; return i; }
Input nextIn() { Input i; i.next = true; return i; }
Input prevIn() { Input i; i.prev = true; return i; }
Input confirmIn() { Input i; i.confirm = true; return i; }
bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
} // namespace

TEST(quickbar_opens_on_press_and_toggles_closed) {
    State s; Settings cfg; auto e = threeEntries();
    CHECK(quickbar::step(s, e, {}, cfg).outcome == Outcome::None);
    CHECK(!s.open);
    CHECK(quickbar::step(s, e, openIn(), cfg).outcome == Outcome::Opened);
    CHECK(s.open);
    CHECK(quickbar::step(s, e, openIn(), cfg).outcome == Outcome::Closed);   // press again: closes (toggle mode)
    CHECK(!s.open);
}

TEST(quickbar_does_not_open_with_nothing_to_offer) {
    State s; Settings cfg;
    CHECK(quickbar::step(s, {}, openIn(), cfg).outcome == Outcome::None);
    CHECK(!s.open);
    // open, then every entry disappears: it closes itself
    auto e = threeEntries();
    quickbar::step(s, e, openIn(), cfg);
    CHECK(quickbar::step(s, {}, {}, cfg).outcome == Outcome::Closed);
    CHECK(!s.open);
}

TEST(quickbar_hold_mode_closes_on_release_not_on_second_press) {
    State s; Settings cfg; cfg.holdToOpen = true; auto e = threeEntries();
    quickbar::step(s, e, openIn(), cfg);
    CHECK(s.open);
    CHECK(quickbar::step(s, e, nextIn(), cfg).outcome == Outcome::Moved);
    Input rel; rel.openReleased = true;
    CHECK(quickbar::step(s, e, rel, cfg).outcome == Outcome::Closed);
    CHECK(!s.open);
}

TEST(quickbar_explicit_close_action) {
    State s; Settings cfg; auto e = threeEntries();
    quickbar::step(s, e, openIn(), cfg);
    Input c; c.closePressed = true;
    CHECK(quickbar::step(s, e, c, cfg).outcome == Outcome::Closed);
    CHECK(!s.open);
}

TEST(quickbar_selection_wraps_both_ways) {
    State s; Settings cfg; auto e = threeEntries();
    quickbar::step(s, e, openIn(), cfg);
    CHECK_EQ(s.selected, 0);
    quickbar::step(s, e, prevIn(), cfg);
    CHECK_EQ(s.selected, 2);                        // 0 - 1 wraps to the end
    quickbar::step(s, e, nextIn(), cfg);
    CHECK_EQ(s.selected, 0);                        // end + 1 wraps to the start
    quickbar::step(s, e, nextIn(), cfg);
    quickbar::step(s, e, nextIn(), cfg);
    CHECK_EQ(s.selected, 2);
    CHECK_EQ(s.selectedId, std::string("orbit_lock"));
    Input both; both.prev = both.next = true;       // cancel out
    CHECK(quickbar::step(s, e, both, cfg).outcome == Outcome::None);
    CHECK_EQ(s.selected, 2);
    CHECK_EQ(quickbar::wrapIndex(0, 0, 1), 0);      // empty list never divides by zero
    CHECK_EQ(quickbar::wrapIndex(1, 3, -5), 2);
}

TEST(quickbar_moving_does_not_need_the_bar_closed_state) {
    State s; Settings cfg; auto e = threeEntries();
    CHECK(quickbar::step(s, e, nextIn(), cfg).outcome == Outcome::None);   // closed: navigation keys do nothing
    CHECK_EQ(s.selected, 0);
    CHECK(quickbar::step(s, e, confirmIn(), cfg).outcome == Outcome::None);
}

TEST(quickbar_selection_follows_id_when_the_list_is_rebuilt) {
    State s; Settings cfg; auto e = threeEntries();
    quickbar::step(s, e, openIn(), cfg);
    quickbar::step(s, e, nextIn(), cfg);
    CHECK_EQ(s.selectedId, std::string("warp"));
    // a new weapon slot appears in front: the selection stays on "warp"
    auto grown = e;
    grown.insert(grown.begin() + 1, Entry{"weapon_2", "BEAM", "", Kind::Instant, true, false, 0});
    quickbar::step(s, grown, {}, cfg);
    CHECK_EQ(s.selected, 2);
    CHECK_EQ(s.selectedId, std::string("warp"));
    // the selected entry vanishes: the index is clamped and the id re-pointed
    std::vector<Entry> shrunk = {e[0]};
    quickbar::step(s, shrunk, {}, cfg);
    CHECK_EQ(s.selected, 0);
    CHECK_EQ(s.selectedId, std::string("weapon_1"));
}

TEST(quickbar_confirm_fires_available_entries_and_refuses_greyed_ones) {
    State s; Settings cfg; auto e = threeEntries();
    quickbar::step(s, e, openIn(), cfg);
    auto r = quickbar::step(s, e, confirmIn(), cfg);
    CHECK(r.outcome == Outcome::Activated);
    CHECK_EQ(r.activated, std::string("weapon_1"));
    CHECK(s.open);                                  // closeOnUse off by default: stays open
    quickbar::step(s, e, prevIn(), cfg);            // orbit_lock: unavailable
    r = quickbar::step(s, e, confirmIn(), cfg);
    CHECK(r.outcome == Outcome::Refused);
    CHECK(r.activated.empty());
    CHECK(near(quickbar::cooldownLeft(s, "orbit_lock"), 0));   // a refused press starts no cooldown
}

TEST(quickbar_cooldown_blocks_refire_and_runs_down) {
    State s; Settings cfg; auto e = threeEntries();
    quickbar::step(s, e, openIn(), cfg);
    quickbar::step(s, e, nextIn(), cfg);            // warp, 0.5 s cooldown
    CHECK(quickbar::step(s, e, confirmIn(), cfg).outcome == Outcome::Activated);
    CHECK(near(quickbar::cooldownLeft(s, "warp"), 0.5f));
    CHECK(near(quickbar::cooldownFraction(s, e[1]), 1.0f));
    CHECK(!quickbar::usable(s, e[1]));
    CHECK(quickbar::step(s, e, confirmIn(), cfg).outcome == Outcome::Refused);
    quickbar::tick(s, 0.25f, cfg);
    CHECK(near(quickbar::cooldownFraction(s, e[1]), 0.5f));
    quickbar::tick(s, 0.3f, cfg);
    CHECK(near(quickbar::cooldownLeft(s, "warp"), 0));
    CHECK(s.cooldowns.empty());                     // expired cooldowns are dropped
    CHECK(quickbar::step(s, e, confirmIn(), cfg).outcome == Outcome::Activated);
    CHECK(near(quickbar::cooldownFraction(s, e[0]), 0));        // no cooldown configured: never shows a fill
}

TEST(quickbar_cooldowns_keep_running_while_closed) {
    State s; Settings cfg; auto e = threeEntries();
    quickbar::step(s, e, openIn(), cfg);
    quickbar::step(s, e, nextIn(), cfg);
    quickbar::step(s, e, confirmIn(), cfg);
    quickbar::step(s, e, openIn(), cfg);            // close
    CHECK(!s.open);
    quickbar::tick(s, 1.0f, cfg);
    CHECK(near(quickbar::cooldownLeft(s, "warp"), 0));
}

TEST(quickbar_close_on_use_option) {
    State s; Settings cfg; cfg.closeOnUse = true; auto e = threeEntries();
    quickbar::step(s, e, openIn(), cfg);
    CHECK(quickbar::step(s, e, confirmIn(), cfg).outcome == Outcome::Activated);
    CHECK(!s.open);
}

TEST(quickbar_idle_timeout_closes_and_any_input_resets_it) {
    State s; Settings cfg; auto e = threeEntries();   // 5 s default
    quickbar::step(s, e, openIn(), cfg);
    CHECK(!quickbar::tick(s, 4.0f, cfg));
    CHECK(near(quickbar::timeoutFraction(s, cfg), 0.2f));
    quickbar::step(s, e, nextIn(), cfg);            // input: timer restarts
    CHECK(near(quickbar::timeoutFraction(s, cfg), 1.0f));
    CHECK(!quickbar::tick(s, 4.9f, cfg));
    CHECK(s.open);
    CHECK(quickbar::tick(s, 0.2f, cfg));            // 5.1 s idle: closed
    CHECK(!s.open);
    CHECK(!quickbar::tick(s, 10.0f, cfg));          // closed: nothing more to close
}

TEST(quickbar_timeout_zero_means_never) {
    State s; Settings cfg; cfg.timeout = 0; auto e = threeEntries();
    quickbar::step(s, e, openIn(), cfg);
    CHECK(!quickbar::tick(s, 1000.0f, cfg));
    CHECK(s.open);
    CHECK(near(quickbar::timeoutFraction(s, cfg), 1.0f));
}

TEST(quickbar_weapon_action_names) {
    CHECK_EQ(quickbar::weaponAction(0), std::string("weapon_1"));
    CHECK_EQ(quickbar::weaponAction(2), std::string("weapon_3"));
    CHECK(quickbar::weaponAction(3).empty());       // no binding past slot 3
    CHECK(quickbar::weaponAction(-1).empty());
}
