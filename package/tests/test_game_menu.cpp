#include <cmath>
#include "core/ui_handler/tabs_layout.h"
#include "tests/test.h"

namespace {
bool close(double a, double b, double tol = 1e-4) { return std::fabs(a - b) <= tol; }
} // namespace

TEST(tabs_layout_shares_the_width_equally) {
    for (int n : {1, 2, 3, 5}) {
        float x = 100, w = 600, gap = 6;
        float end = x;
        for (int i = 0; i < n; i++) {
            core::TabRect r = core::tabRect(i, n, x, w, gap);
            CHECK(close(r.x, i == 0 ? x : end + gap));                   // tabs follow each other with the gap between
            CHECK(r.w > 0);
            end = r.x + r.w;
        }
        CHECK(close(end, x + w));                                        // the last tab ends exactly at the row's right edge
        CHECK(close(core::tabRect(0, n, x, w, gap).w, core::tabRect(n - 1, n, x, w, gap).w));   // equal widths
    }
    core::TabRect bad = core::tabRect(5, 3, 0, 100); CHECK(close(bad.w, 0));           // out of range: empty
    CHECK(close(core::tabRect(0, 0, 0, 100).w, 0));
    CHECK(core::tabRect(0, 3, 0, 5, 6).w >= 0);                          // a row narrower than the gaps never gives a negative width
}

TEST(tabs_hit_testing_and_gaps) {
    float x = 0, w = 300, gap = 6;                                       // 3 tabs of 96
    CHECK_EQ(core::tabAt(10, 3, x, w, gap), 0);
    CHECK_EQ(core::tabAt(150, 3, x, w, gap), 1);
    CHECK_EQ(core::tabAt(290, 3, x, w, gap), 2);
    CHECK_EQ(core::tabAt(99, 3, x, w, gap), -1);                         // in the gap between tab 0 and 1
    CHECK_EQ(core::tabAt(-5, 3, x, w, gap), -1); CHECK_EQ(core::tabAt(400, 3, x, w, gap), -1);
    CHECK_EQ(core::tabAt(10, 0, x, w, gap), -1);
    // every tab's own centre hits that tab
    for (int i = 0; i < 3; i++) { core::TabRect r = core::tabRect(i, 3, x, w, gap); CHECK_EQ(core::tabAt(r.x + r.w / 2, 3, x, w, gap), i); }
}

TEST(tabs_cycle_wraps_and_selection_is_clamped) {
    CHECK_EQ(core::cycleTab(0, 3, +1), 1); CHECK_EQ(core::cycleTab(2, 3, +1), 0);      // next wraps
    CHECK_EQ(core::cycleTab(0, 3, -1), 2); CHECK_EQ(core::cycleTab(1, 3, -1), 0);      // previous wraps
    CHECK_EQ(core::cycleTab(0, 1, +1), 0); CHECK_EQ(core::cycleTab(0, 1, -1), 0);      // a single tab stays
    CHECK_EQ(core::cycleTab(4, 0, +1), 0);                                              // no tabs: 0, no crash
    CHECK_EQ(core::clampTab(7, 3), 2); CHECK_EQ(core::clampTab(-2, 3), 0); CHECK_EQ(core::clampTab(1, 3), 1); CHECK_EQ(core::clampTab(3, 0), 0);
}

#include "ui/game_menu/menu_rules.h"

TEST(game_menu_esc_closes_the_menu_and_is_cancelled_while_held) {
    ui::PauseSwallow s; bool close = false;
    CHECK(!s.step(false, 1.0f, close)); CHECK(!close);                    // menu closed: Esc is not touched (the pause menu handles it)
    CHECK(!s.step(false, 0.0f, close));
    // menu open: the press is a close request and is cancelled
    CHECK(s.step(true, 1.0f, close)); CHECK(close);
    CHECK(s.step(false, 1.0f, close)); CHECK(!close);                     // the menu is closed by now but the key is still down: still cancelled, no new request
    CHECK(s.step(false, 1.0f, close)); CHECK(!close);
    CHECK(!s.step(false, 0.0f, close)); CHECK(!s.active());               // released: back to normal
    CHECK(!s.step(false, 1.0f, close));                                   // the next press (menu closed) reaches the pause menu
    CHECK(!s.step(false, 0.0f, close));
    // a held key that was already down when the menu opened still counts as one press per hold
    ui::PauseSwallow t;
    CHECK(t.step(true, 1.0f, close)); CHECK(close); CHECK(t.step(true, 1.0f, close)); CHECK(!close);   // menu re-opened during the hold: not a second close
    CHECK(!t.step(true, 0.0f, close));
    CHECK(t.step(true, 1.0f, close)); CHECK(close);                       // a fresh press closes again
}

TEST(game_menu_cargo_grid_cursor_is_clamped_at_the_edges) {
    CHECK_EQ(ui::gridStep(-1, 1, 0, 8, 96), 0);                          // nothing selected: start at the first slot
    CHECK_EQ(ui::gridStep(0, 1, 0, 8, 96), 1); CHECK_EQ(ui::gridStep(0, -1, 0, 8, 96), 0);
    CHECK_EQ(ui::gridStep(7, 1, 0, 8, 96), 7);                           // right edge: stays (no wrap into the next row)
    CHECK_EQ(ui::gridStep(3, 0, 1, 8, 96), 11); CHECK_EQ(ui::gridStep(3, 0, -1, 8, 96), 3);
    CHECK_EQ(ui::gridStep(92, 0, 1, 8, 96), 92);                         // bottom row
    CHECK_EQ(ui::gridStep(8, 0, 1, 8, 10), 8); CHECK_EQ(ui::gridStep(1, 0, 1, 8, 10), 9);
    CHECK_EQ(ui::gridStep(7, 0, 1, 8, 10), 9);                           // a short last row: clamped to the last slot
    CHECK_EQ(ui::gridStep(0, 1, 0, 8, 0), -1); CHECK_EQ(ui::gridStep(500, 1, 0, 8, 96), 0);
}

TEST(game_menu_cargo_discard_needs_two_presses_on_the_same_slot) {
    ui::DiscardConfirm d;
    CHECK(!d.press(4, 10.0)); CHECK(d.armed(4, 10.5)); CHECK(!d.armed(5, 10.5));
    CHECK(d.press(4, 11.0));                                             // second press within the window: discard
    CHECK(!d.armed(4, 11.0));
    CHECK(!d.press(4, 20.0)); CHECK(!d.press(4, 24.0));                  // too slow: the second press only re-arms ...
    CHECK(d.press(4, 25.0));                                             // ... and a quick third one discards
    CHECK(!d.press(2, 30.0)); CHECK(!d.press(3, 30.5)); CHECK(d.armed(3, 30.5)); CHECK(!d.armed(2, 30.5));   // another slot re-arms, never discards
    d.reset(); CHECK(!d.press(3, 31.0));
    CHECK(!d.press(-1, 31.2)); CHECK(!d.press(3, 31.4));                 // "no slot" disarms
}
