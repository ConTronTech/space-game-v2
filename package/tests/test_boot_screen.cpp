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
