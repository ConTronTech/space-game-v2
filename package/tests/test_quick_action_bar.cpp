#include <cmath>
#include "tests/test.h"
#include "ui/quick_action_bar/quick_bar_rules.h"

namespace {
using quickbar::Entry;
using quickbar::Input;
using quickbar::Kind;
using quickbar::Outcome;
using quickbar::Row;
using quickbar::Settings;
using quickbar::State;

// The shape the module builds: WEAPONS (3), SHIP SYSTEMS (2), ITEMS / PERKS (1, a greyed passive perk).
std::vector<Row> rowsFull() {
    return {
        {"WEAPONS", {
            {"weapon_1", "BLASTER", "", Kind::Instant, true, true, 0},
            {"weapon_2", "MINING BEAM", "", Kind::Instant, true, false, 0},
            {"weapon_3", "MISSILES", "x3", Kind::Instant, true, false, 0},
        }},
        {"SHIP SYSTEMS", {
            {"warp", "WARP", "", Kind::Toggle, true, false, 0.5f},
            {"orbit_lock", "ORBIT LOCK", "no body in range", Kind::Toggle, false, false, 0.5f},
        }},
        {"ITEMS / PERKS", {
            {"ore_scanner", "ORE SCANNER", "PASSIVE - ALWAYS ON", Kind::Instant, false, true, 0},
        }},
    };
}
// A single row, for the rules that do not care about rows (open/close, cooldowns, timeout).
std::vector<Row> oneRow() {
    return {{"SHIP SYSTEMS", {
        {"weapon_1", "BLASTER", "", Kind::Instant, true, true, 0},
        {"warp", "WARP", "", Kind::Toggle, true, false, 0.5f},
        {"orbit_lock", "ORBIT LOCK", "no body in range", Kind::Toggle, false, false, 0.5f},
    }}};
}
Input openIn() { Input i; i.openPressed = true; return i; }
Input nextIn() { Input i; i.next = true; return i; }
Input prevIn() { Input i; i.prev = true; return i; }
Input leftIn() { Input i; i.left = true; return i; }
Input rightIn() { Input i; i.right = true; return i; }
Input upIn() { Input i; i.up = true; return i; }
Input downIn() { Input i; i.down = true; return i; }
Input confirmIn() { Input i; i.confirm = true; return i; }
Input hoverIn(const std::string& id, bool moved = true) { Input i; i.hoverId = id; i.mouseMoved = moved; return i; }
Input clickIn(const std::string& id) { Input i; i.hoverId = id; i.click = true; return i; }
bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
} // namespace

TEST(quickbar_opens_on_press_and_toggles_closed) {
    State s; Settings cfg; auto e = oneRow();
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
    std::vector<Row> emptyRows = {{"WEAPONS", {}}, {"ITEMS / PERKS", {}}};   // rows with no entries count as nothing
    CHECK(quickbar::step(s, emptyRows, openIn(), cfg).outcome == Outcome::None);
    CHECK(!s.open);
    // open, then every entry disappears: it closes itself
    auto e = oneRow();
    quickbar::step(s, e, openIn(), cfg);
    CHECK(quickbar::step(s, {}, {}, cfg).outcome == Outcome::Closed);
    CHECK(!s.open);
}

TEST(quickbar_hold_mode_closes_on_release_not_on_second_press) {
    State s; Settings cfg; cfg.holdToOpen = true; auto e = oneRow();
    quickbar::step(s, e, openIn(), cfg);
    CHECK(s.open);
    CHECK(quickbar::step(s, e, nextIn(), cfg).outcome == Outcome::Moved);
    Input rel; rel.openReleased = true;
    CHECK(quickbar::step(s, e, rel, cfg).outcome == Outcome::Closed);
    CHECK(!s.open);
}

TEST(quickbar_explicit_close_action) {
    State s; Settings cfg; auto e = oneRow();
    quickbar::step(s, e, openIn(), cfg);
    Input c; c.closePressed = true;
    CHECK(quickbar::step(s, e, c, cfg).outcome == Outcome::Closed);
    CHECK(!s.open);
}

TEST(quickbar_entry_selection_wraps_within_the_row) {
    State s; Settings cfg; auto e = oneRow();
    quickbar::step(s, e, openIn(), cfg);
    CHECK_EQ(s.col, 0);
    quickbar::step(s, e, prevIn(), cfg);
    CHECK_EQ(s.col, 2);                             // 0 - 1 wraps to the end
    quickbar::step(s, e, nextIn(), cfg);
    CHECK_EQ(s.col, 0);                             // end + 1 wraps to the start
    quickbar::step(s, e, rightIn(), cfg);           // A/D do the same as , / .
    quickbar::step(s, e, rightIn(), cfg);
    CHECK_EQ(s.col, 2);
    CHECK_EQ(s.selectedId, std::string("orbit_lock"));
    quickbar::step(s, e, leftIn(), cfg);
    CHECK_EQ(s.selectedId, std::string("warp"));
    Input both; both.prev = both.next = true;       // cancel out
    CHECK(quickbar::step(s, e, both, cfg).outcome == Outcome::None);
    Input lr; lr.left = lr.right = true;
    CHECK(quickbar::step(s, e, lr, cfg).outcome == Outcome::None);
    CHECK_EQ(s.col, 1);
    CHECK_EQ(quickbar::wrapIndex(0, 0, 1), 0);      // empty list never divides by zero
    CHECK_EQ(quickbar::wrapIndex(1, 3, -5), 2);
}

TEST(quickbar_left_right_never_leave_the_row) {
    State s; Settings cfg; auto e = rowsFull();
    quickbar::step(s, e, openIn(), cfg);
    quickbar::step(s, e, downIn(), cfg);            // SHIP SYSTEMS
    CHECK_EQ(s.row, 1);
    quickbar::step(s, e, rightIn(), cfg);
    quickbar::step(s, e, rightIn(), cfg);           // past ORBIT LOCK: back to WARP, same row
    CHECK_EQ(s.row, 1);
    CHECK_EQ(s.selectedId, std::string("warp"));
    // a one-entry row: left/right has nowhere to go, no Moved (no click sound)
    quickbar::step(s, e, downIn(), cfg);
    CHECK_EQ(s.row, 2);
    CHECK(quickbar::step(s, e, rightIn(), cfg).outcome == Outcome::None);
    CHECK_EQ(s.selectedId, std::string("ore_scanner"));
}

TEST(quickbar_rows_wrap_and_keep_the_column_where_it_fits) {
    State s; Settings cfg; auto e = rowsFull();
    quickbar::step(s, e, openIn(), cfg);
    quickbar::step(s, e, rightIn(), cfg);
    quickbar::step(s, e, rightIn(), cfg);           // WEAPONS col 2 (MISSILES)
    CHECK(quickbar::step(s, e, downIn(), cfg).outcome == Outcome::Moved);
    CHECK_EQ(s.row, 1);
    CHECK_EQ(s.col, 1);                             // SHIP SYSTEMS has 2 entries: clamped to the last
    CHECK_EQ(s.selectedId, std::string("orbit_lock"));
    quickbar::step(s, e, downIn(), cfg);
    CHECK_EQ(s.row, 2);
    CHECK_EQ(s.col, 0);
    quickbar::step(s, e, downIn(), cfg);            // past the last row: wraps to the first
    CHECK_EQ(s.row, 0);
    CHECK_EQ(s.selectedId, std::string("weapon_1"));
    quickbar::step(s, e, upIn(), cfg);              // above the first row: wraps to the last
    CHECK_EQ(s.row, 2);
    Input both; both.up = both.down = true;         // cancel out
    CHECK(quickbar::step(s, e, both, cfg).outcome == Outcome::None);
    CHECK_EQ(s.row, 2);
}

TEST(quickbar_up_down_skip_empty_rows_and_do_nothing_with_one_row) {
    State s; Settings cfg;
    std::vector<Row> e = {
        {"WEAPONS", {{"weapon_1", "BLASTER", "", Kind::Instant, true, true, 0}}},
        {"SHIP SYSTEMS", {}},
        {"ITEMS / PERKS", {{"ore_scanner", "ORE SCANNER", "", Kind::Instant, false, true, 0}}},
    };
    quickbar::step(s, e, openIn(), cfg);
    quickbar::step(s, e, downIn(), cfg);
    CHECK_EQ(s.row, 2);                             // the empty row in between is skipped
    quickbar::step(s, e, downIn(), cfg);
    CHECK_EQ(s.row, 0);
    auto single = oneRow();
    State t;
    quickbar::step(t, single, openIn(), cfg);
    CHECK(quickbar::step(t, single, downIn(), cfg).outcome == Outcome::None);
    CHECK_EQ(t.row, 0);
    CHECK_EQ(t.col, 0);
}

TEST(quickbar_moving_does_not_need_the_bar_closed_state) {
    State s; Settings cfg; auto e = rowsFull();
    CHECK(quickbar::step(s, e, nextIn(), cfg).outcome == Outcome::None);   // closed: navigation keys do nothing
    CHECK(quickbar::step(s, e, downIn(), cfg).outcome == Outcome::None);   // (W/S/A/D only fly the ship while the bar is closed)
    CHECK_EQ(s.row, 0);
    CHECK_EQ(s.col, 0);
    CHECK(quickbar::step(s, e, confirmIn(), cfg).outcome == Outcome::None);
    CHECK(quickbar::step(s, e, clickIn("warp"), cfg).outcome == Outcome::None);
    CHECK(s.cooldowns.empty());
}

TEST(quickbar_selection_follows_id_when_the_rows_are_rebuilt) {
    State s; Settings cfg; auto e = rowsFull();
    quickbar::step(s, e, openIn(), cfg);
    quickbar::step(s, e, downIn(), cfg);
    CHECK_EQ(s.selectedId, std::string("warp"));
    // the WEAPONS row disappears (combat module off): the selection stays on "warp", now in row 0
    std::vector<Row> noWeapons(e.begin() + 1, e.end());
    quickbar::step(s, noWeapons, {}, cfg);
    CHECK_EQ(s.row, 0);
    CHECK_EQ(s.col, 0);
    CHECK_EQ(s.selectedId, std::string("warp"));
    // a new ship system appears in front of it: the selection still follows warp
    auto grown = noWeapons;
    grown[0].entries.insert(grown[0].entries.begin(), Entry{"shield", "SHIELD", "", Kind::Toggle, true, true, 0.5f});
    quickbar::step(s, grown, {}, cfg);
    CHECK_EQ(s.col, 1);
    CHECK_EQ(s.selectedId, std::string("warp"));
    // the selected entry vanishes: the (row, col) is clamped and the id re-pointed
    std::vector<Row> shrunk = {{"WEAPONS", {e[0].entries[0]}}};
    quickbar::step(s, shrunk, {}, cfg);
    CHECK_EQ(s.row, 0);
    CHECK_EQ(s.col, 0);
    CHECK_EQ(s.selectedId, std::string("weapon_1"));
    // the selected ROW empties out but stays in the list: moves on to the next non-empty row
    State t; t.selectedId = "gone"; t.row = 1; t.col = 0;
    std::vector<Row> holey = {{"A", {e[0].entries[0]}}, {"B", {}}, {"C", {e[2].entries[0]}}};
    quickbar::resync(t, holey);
    CHECK_EQ(t.row, 2);
    CHECK_EQ(t.selectedId, std::string("ore_scanner"));
}

TEST(quickbar_confirm_fires_available_entries_and_refuses_greyed_ones) {
    State s; Settings cfg; auto e = oneRow();
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

TEST(quickbar_mouse_hover_selects_only_when_the_mouse_moved) {
    State s; Settings cfg; auto e = rowsFull();
    quickbar::step(s, e, openIn(), cfg);
    CHECK(quickbar::step(s, e, hoverIn("orbit_lock"), cfg).outcome == Outcome::Moved);
    CHECK_EQ(s.row, 1);
    CHECK_EQ(s.col, 1);
    CHECK_EQ(s.selectedId, std::string("orbit_lock"));
    // the cursor stays parked on ORBIT LOCK while the keyboard moves on: the keyboard wins, the parked cursor does not pull it back
    quickbar::step(s, e, upIn(), cfg);
    CHECK_EQ(s.row, 0);
    CHECK(quickbar::step(s, e, hoverIn("orbit_lock", false), cfg).outcome == Outcome::None);
    CHECK_EQ(s.row, 0);
    // keyboard and a mouse move in the same frame: the keyboard wins that frame
    Input both = hoverIn("orbit_lock"); both.right = true;
    quickbar::step(s, e, both, cfg);
    CHECK_EQ(s.row, 0);
    CHECK_EQ(s.col, 2);
    // hovering the already-selected entry is not a move (no repeated click sound)
    CHECK(quickbar::step(s, e, hoverIn("weapon_3"), cfg).outcome == Outcome::None);
    // an unknown / stale id (a tile from a rebuilt layout) is ignored
    CHECK(quickbar::step(s, e, hoverIn("no_such_entry"), cfg).outcome == Outcome::None);
    CHECK_EQ(s.selectedId, std::string("weapon_3"));
    // hovering counts as input for the idle timer
    quickbar::tick(s, 3.0f, cfg);
    quickbar::step(s, e, hoverIn("warp"), cfg);
    CHECK(near(quickbar::timeoutFraction(s, cfg), 1.0f));
}

TEST(quickbar_mouse_click_selects_and_activates) {
    State s; Settings cfg; auto e = rowsFull();
    quickbar::step(s, e, openIn(), cfg);
    auto r = quickbar::step(s, e, clickIn("warp"), cfg);
    CHECK(r.outcome == Outcome::Activated);
    CHECK_EQ(r.activated, std::string("warp"));
    CHECK_EQ(s.selectedId, std::string("warp"));
    CHECK(near(quickbar::cooldownLeft(s, "warp"), 0.5f));
    // a greyed entry: selected, but refused
    r = quickbar::step(s, e, clickIn("ore_scanner"), cfg);
    CHECK(r.outcome == Outcome::Refused);
    CHECK_EQ(s.selectedId, std::string("ore_scanner"));
    // a click off every tile does nothing
    Input miss; miss.click = true;
    CHECK(quickbar::step(s, e, miss, cfg).outcome == Outcome::None);
    CHECK(s.open);
}

TEST(quickbar_cooldown_blocks_refire_and_runs_down) {
    State s; Settings cfg; auto e = oneRow();
    const Entry& warp = e[0].entries[1];
    quickbar::step(s, e, openIn(), cfg);
    quickbar::step(s, e, nextIn(), cfg);            // warp, 0.5 s cooldown
    CHECK(quickbar::step(s, e, confirmIn(), cfg).outcome == Outcome::Activated);
    CHECK(near(quickbar::cooldownLeft(s, "warp"), 0.5f));
    CHECK(near(quickbar::cooldownFraction(s, warp), 1.0f));
    CHECK(!quickbar::usable(s, warp));
    CHECK(quickbar::step(s, e, confirmIn(), cfg).outcome == Outcome::Refused);
    CHECK(quickbar::step(s, e, clickIn("warp"), cfg).outcome == Outcome::Refused);   // the mouse obeys the cooldown too
    quickbar::tick(s, 0.25f, cfg);
    CHECK(near(quickbar::cooldownFraction(s, warp), 0.5f));
    quickbar::tick(s, 0.3f, cfg);
    CHECK(near(quickbar::cooldownLeft(s, "warp"), 0));
    CHECK(s.cooldowns.empty());                     // expired cooldowns are dropped
    CHECK(quickbar::step(s, e, confirmIn(), cfg).outcome == Outcome::Activated);
    CHECK(near(quickbar::cooldownFraction(s, e[0].entries[0]), 0));   // no cooldown configured: never shows a fill
}

TEST(quickbar_cooldowns_keep_running_while_closed) {
    State s; Settings cfg; auto e = oneRow();
    quickbar::step(s, e, openIn(), cfg);
    quickbar::step(s, e, nextIn(), cfg);
    quickbar::step(s, e, confirmIn(), cfg);
    quickbar::step(s, e, openIn(), cfg);            // close
    CHECK(!s.open);
    quickbar::tick(s, 1.0f, cfg);
    CHECK(near(quickbar::cooldownLeft(s, "warp"), 0));
}

TEST(quickbar_close_on_use_option) {
    State s; Settings cfg; cfg.closeOnUse = true; auto e = rowsFull();
    quickbar::step(s, e, openIn(), cfg);
    CHECK(quickbar::step(s, e, confirmIn(), cfg).outcome == Outcome::Activated);
    CHECK(!s.open);
    quickbar::step(s, e, openIn(), cfg);
    CHECK(quickbar::step(s, e, clickIn("weapon_2"), cfg).outcome == Outcome::Activated);   // a click closes it too
    CHECK(!s.open);
}

TEST(quickbar_idle_timeout_closes_and_any_input_resets_it) {
    State s; Settings cfg; auto e = rowsFull();       // 5 s default
    quickbar::step(s, e, openIn(), cfg);
    CHECK(!quickbar::tick(s, 4.0f, cfg));
    CHECK(near(quickbar::timeoutFraction(s, cfg), 0.2f));
    quickbar::step(s, e, nextIn(), cfg);            // input: timer restarts
    CHECK(near(quickbar::timeoutFraction(s, cfg), 1.0f));
    quickbar::tick(s, 4.0f, cfg);
    quickbar::step(s, e, downIn(), cfg);            // row keys count too
    CHECK(near(quickbar::timeoutFraction(s, cfg), 1.0f));
    CHECK(!quickbar::tick(s, 4.9f, cfg));
    CHECK(s.open);
    CHECK(quickbar::tick(s, 0.2f, cfg));            // 5.1 s idle: closed
    CHECK(!s.open);
    CHECK(!quickbar::tick(s, 10.0f, cfg));          // closed: nothing more to close
}

TEST(quickbar_timeout_zero_means_never) {
    State s; Settings cfg; cfg.timeout = 0; auto e = oneRow();
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
