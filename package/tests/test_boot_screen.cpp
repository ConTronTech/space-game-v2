#include <cmath>
#include "core/boot_screen/boot_screen_rules.h"
#include "tests/test.h"

using namespace core::boot;

TEST(boot_screen_track_rect_centred_and_degenerate) {
    Rect t = trackRect(1280, 720);
    CHECK(t.w > 0 && t.h > 0);
    CHECK(std::fabs((t.x + t.w / 2) - 640.0f) < 1e-3f);   // centred horizontally
    CHECK_EQ(trackRect(0, 720).w, 0.0f);
    CHECK_EQ(trackRect(1280, -1).w, 0.0f);
}

TEST(boot_screen_progress_fraction_clamps) {
    CHECK_EQ(progressFraction(0, 10), 0.0f);
    CHECK_EQ(progressFraction(5, 10), 0.5f);
    CHECK_EQ(progressFraction(10, 10), 1.0f);
    CHECK_EQ(progressFraction(15, 10), 1.0f);    // over 100%: clamped, never crashes
    CHECK_EQ(progressFraction(5, 0), 0.0f);      // no total yet: 0, not a divide-by-zero
    CHECK_EQ(progressFraction(5, -3), 0.0f);
}

TEST(boot_screen_label_pos_above_the_track_left_aligned) {
    Rect track = trackRect(1000, 600);
    Rect lbl = labelPos(track, 16.0f);
    CHECK_EQ(lbl.x, track.x);          // left-aligned with the track
    CHECK(lbl.y < track.y);            // sits above it
    Rect none = labelPos(Rect{}, 16.0f);
    CHECK_EQ(none.x, 0.0f);
    CHECK_EQ(none.y, 0.0f);
}

TEST(boot_screen_next_draw_draws_the_final_full_frame_exactly_once) {
    // Not reached total yet: keeps drawing, never stops.
    DrawDecision d = nextDraw(false, false);
    CHECK(d.draw && !d.stopAfter);
    // The last module's event just arrived (reachedTotal true, not done yet): still draws (the 100% frame), THEN stops.
    d = nextDraw(false, true);
    CHECK(d.draw && d.stopAfter);
    // Once done, never draws again, regardless of reachedTotal.
    d = nextDraw(true, true);
    CHECK(!d.draw && d.stopAfter);
    d = nextDraw(true, false);
    CHECK(!d.draw && d.stopAfter);
}

TEST(boot_screen_fill_rect_grows_with_fraction_never_wider_than_track) {
    Rect track = trackRect(1000, 600);
    CHECK_EQ(fillRect(track, 0.0f).w, 0.0f);
    CHECK(std::fabs(fillRect(track, 0.5f).w - track.w * 0.5f) < 1e-3f);
    CHECK(std::fabs(fillRect(track, 1.0f).w - track.w) < 1e-3f);
    CHECK_EQ(fillRect(track, 2.0f).w, track.w);    // over 100%: clamped to the track's own width
    CHECK_EQ(fillRect(track, -1.0f).w, 0.0f);
    CHECK_EQ(fillRect(track, 0.3f).x, track.x);    // fill starts at the track's left edge
    CHECK_EQ(fillRect(track, 0.3f).y, track.y);
}

TEST(boot_screen_sub_progress_moves_within_the_modules_own_slice) {
    // module 3 of 10 is inside its init() (engine::BootStep): the bar sits between 3/10 and 4/10
    CHECK_EQ(progressFraction(3, 10, 0.0f), progressFraction(3, 10));
    CHECK(std::fabs(progressFraction(3, 10, 0.5f) - 0.35f) < 1e-6f);
    CHECK(std::fabs(progressFraction(3, 10, 1.0f) - 0.4f) < 1e-6f);
    CHECK(std::fabs(progressFraction(3, 10, 7.0f) - 0.4f) < 1e-6f);    // a bad fraction never jumps past the slice
    CHECK_EQ(progressFraction(3, 10, -1.0f), progressFraction(3, 10));  // ... or goes backwards
    CHECK_EQ(progressFraction(10, 10, 1.0f), 1.0f);                     // never past 100%
    CHECK_EQ(progressFraction(2, 0, 0.5f), 0.0f);                       // no total yet: 0, not a divide-by-zero
}
