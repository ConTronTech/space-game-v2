// Pure display logic with fake data: aspect classes, display choice, mode / resolution picking, placement, Hor+ FOV, UI scale. No SDL, no window.
#include <cmath>
#include "core/render_engine/fov_rules.h"
#include "core/ui_handler/ui_handler.h"
#include "core/window/display_rules.h"
#include "tests/test.h"

namespace {
using namespace core::display;
bool nearf(double a, double b, double eps = 1e-3) { return std::fabs(a - b) < eps; }

DisplayInfo disp(int i, const char* name, int x, int y, int w, int h, int hz, std::vector<Mode> modes = {}) {
    DisplayInfo d; d.index = i; d.name = name; d.x = x; d.y = y; d.w = w; d.h = h; d.refreshHz = hz; d.modes = std::move(modes); return d;
}
// the retro rig from docs/DEVICES.md: the CRT at 0,0 and the laptop panel to its right, 256 lower
std::vector<DisplayInfo> rig() {
    return {disp(0, "VGA-1", 0, 0, 1280, 1024, 85, {{1280, 1024, 85}, {1280, 1024, 60}, {1024, 768, 85}, {1024, 768, 60}, {800, 600, 85}, {800, 600, 60}, {640, 480, 60}, {320, 240, 60}}),
            disp(1, "LVDS-1", 1280, 256, 1366, 768, 60, {{1366, 768, 60}, {1280, 720, 60}, {1024, 768, 60}, {800, 600, 60}})};
}
}

TEST(display_aspect_classes) {
    CHECK(classifyAspect(1280, 1024) == Aspect::R5_4);
    CHECK(classifyAspect(1024, 768) == Aspect::R4_3 && classifyAspect(800, 600) == Aspect::R4_3 && classifyAspect(640, 480) == Aspect::R4_3);
    CHECK(classifyAspect(1280, 800) == Aspect::R16_10 && classifyAspect(1920, 1200) == Aspect::R16_10);
    CHECK(classifyAspect(1920, 1080) == Aspect::R16_9 && classifyAspect(1280, 720) == Aspect::R16_9 && classifyAspect(1600, 900) == Aspect::R16_9);
    CHECK(classifyAspect(1366, 768) == Aspect::R16_9 && classifyAspect(1360, 768) == Aspect::R16_9);      // the laptop panel is not exactly 16:9
    CHECK(classifyAspect(2560, 1080) == Aspect::UltraWide && classifyAspect(3440, 1440) == Aspect::UltraWide && classifyAspect(5120, 1440) == Aspect::UltraWide);
    CHECK(classifyAspect(1280, 854) == Aspect::Other && classifyAspect(1000, 1000) == Aspect::Other);     // 3:2, square
    CHECK(classifyAspect(0, 0) == Aspect::Other && classifyAspect(-5, 10) == Aspect::Other);
    CHECK(std::string(aspectName(Aspect::R5_4)) == "5:4" && std::string(aspectName(Aspect::UltraWide)) == "ultrawide");
}
TEST(display_retro_hint_is_only_a_hint) {
    auto r = rig();
    CHECK(isRetro(r[0]));                                                     // 5:4, 85 Hz, 1280 wide
    CHECK(!isRetro(r[1]));                                                    // 16:9 laptop panel
    CHECK(isRetro(disp(0, "crt", 0, 0, 1024, 768, 60)));
    CHECK(!isRetro(disp(0, "lcd 4:3", 0, 0, 1024, 768, 30)));                 // needs >= 60 Hz
    CHECK(!isRetro(disp(0, "big 5:4", 0, 0, 1600, 1280, 60)));                // and <= 1280 wide
    CHECK(!isRetro(disp(0, "hd", 0, 0, 1920, 1080, 60)));
}
TEST(display_choice_auto_index_out_of_range_and_mouse) {
    auto r = rig();
    Choice c = chooseDisplay(r, "auto", -1, false, 0, 0);
    CHECK_EQ(c.index, 0);                                                     // no mouse: the primary
    CHECK(!c.fellBack && c.why.find("primary") != std::string::npos);
    c = chooseDisplay(r, "auto", -1, true, 1500, 400);                        // the mouse is on the laptop panel
    CHECK_EQ(c.index, 1);
    CHECK(c.why.find("mouse") != std::string::npos);
    c = chooseDisplay(r, "auto", -1, true, 100, 100);
    CHECK_EQ(c.index, 0);
    c = chooseDisplay(r, "auto", -1, true, 1300, 100);                        // above the panel (it starts at y = 256) and right of the CRT: on neither
    CHECK_EQ(c.index, 0);
    CHECK(c.why.find("primary") != std::string::npos);
    c = chooseDisplay(r, "", -1, false, 0, 0);
    CHECK_EQ(c.index, 0);                                                     // an empty setting is auto
    c = chooseDisplay(r, "1", -1, true, 100, 100);
    CHECK_EQ(c.index, 1);                                                     // the setting beats the mouse
    CHECK(c.why.find("setting") != std::string::npos);
    c = chooseDisplay(r, "1", 0, true, 1500, 400);
    CHECK_EQ(c.index, 0);                                                     // the flag beats the setting
    CHECK(c.why.find("flag") != std::string::npos);
    c = chooseDisplay(r, "5", -1, true, 1500, 400);                           // does not exist: fall back to auto (the mouse), say so
    CHECK(c.fellBack && c.index == 1);
    CHECK(c.why.find("does not exist") != std::string::npos && c.why.find("2 found") != std::string::npos);
    c = chooseDisplay(r, "auto", 7, false, 0, 0);
    CHECK(c.fellBack && c.index == 0);
    c = chooseDisplay(r, "banana", -1, false, 0, 0);                          // garbage: auto, flagged
    CHECK(c.fellBack && c.index == 0 && c.why.find("banana") != std::string::npos);
    c = chooseDisplay(r, "-3", -1, false, 0, 0);
    CHECK(c.fellBack && c.index == 0);
    c = chooseDisplay({}, "auto", -1, true, 0, 0);
    CHECK(c.index == 0 && c.why.find("no displays") != std::string::npos);
    CHECK_EQ(displayContaining(r, 1279, 1023), 0);
    CHECK_EQ(displayContaining(r, 1280, 300), 1);                             // edges: x and y are inclusive at the origin, exclusive at the far side
    CHECK_EQ(displayContaining(r, 2646, 300), -1);
}
TEST(display_video_mode_and_resolution_parsing) {
    VideoMode m;
    CHECK(parseVideoMode("borderless", m) && m == VideoMode::Borderless);
    CHECK(parseVideoMode("exclusive", m) && m == VideoMode::Exclusive);
    CHECK(parseVideoMode("windowed", m) && m == VideoMode::Windowed);
    CHECK(!parseVideoMode("fullscreen", m) && !parseVideoMode("", m));
    Res r;
    CHECK(parseResolution("1024x768@85", r) && r.w == 1024 && r.h == 768 && r.hz == 85);
    CHECK(parseResolution("1280x720", r) && r.w == 1280 && r.h == 720 && r.hz == 0);
    CHECK(parseResolution("800X600", r) && r.w == 800);
    CHECK(!parseResolution("1024", r) && !parseResolution("1024x", r) && !parseResolution("axb", r) && !parseResolution("", r));
    CHECK(!parseResolution("1024x768@", r) && !parseResolution("1024x768@abc", r) && !parseResolution("1024x768#85", r));
    CHECK(!parseResolution("10x10", r) && !parseResolution("99999x99999", r) && !parseResolution("1024x768@-5", r));
    CHECK_EQ(resolutionText({1024, 768, 85}), std::string("1024x768@85"));
    CHECK_EQ(resolutionText({1280, 720, 0}), std::string("1280x720"));
}
TEST(display_closest_mode) {
    auto r = rig();
    Mode m;
    bool exact = false;
    CHECK(closestMode(r[0].modes, {1024, 768, 85}, m, &exact) && exact && (m == Mode{1024, 768, 85}));
    CHECK(closestMode(r[0].modes, {1024, 768, 75}, m, &exact) && !exact && m.w == 1024 && m.h == 768 && m.hz == 85);   // same size, nearest refresh (85 is 10 away, 60 is 15)
    CHECK(closestMode(r[0].modes, {1024, 768, 0}, m, &exact) && exact && m.hz == 85);                                    // no refresh asked: the highest
    CHECK(closestMode(r[0].modes, {1152, 864, 60}, m, &exact) && !exact && m.w == 1024 && m.h == 768 && m.hz == 60);     // nearest size (1024x768 beats 1280x1024), then the refresh
    CHECK(closestMode(r[0].modes, {640, 400, 60}, m, &exact) && m.w == 640 && m.h == 480);
    CHECK(closestMode(r[0].modes, {9999, 9999, 0}, m, &exact) && m.w == 1280 && m.h == 1024 && !exact);                  // bigger than anything: the biggest
    CHECK(!closestMode({}, {1024, 768, 60}, m));                                                                         // no modes: false, no crash
}
TEST(display_resolution_list_dedupes_filters_and_sorts) {
    auto r = rig();
    auto list = selectableResolutions(r[0].modes);
    CHECK_EQ(list.size(), (size_t)4);                                          // 1280x1024, 1024x768, 800x600, 640x480; 320x240 is below the minimum
    CHECK(list[0].w == 1280 && list[0].h == 1024 && list[0].hz == 85);         // one entry per size, with its highest refresh
    CHECK(list[1].w == 1024 && list[2].w == 800 && list[3].w == 640);
    auto both = selectableResolutions({{1280, 720, 60}, {1280, 720, 120}, {1920, 1080, 60}, {1280, 800, 60}, {1024, 768, 60}, {639, 480, 60}, {800, 479, 60}});
    CHECK_EQ(both.size(), (size_t)4);
    CHECK(both[0].w == 1920 && both[1].w == 1280 && both[1].h == 800 && both[2].h == 720 && both[2].hz == 120 && both[3].w == 1024);   // by pixel count, biggest first
    CHECK(selectableResolutions({}).empty());
    CHECK(selectableResolutions(r[0].modes, 1000, 700).size() == 2);           // a stricter minimum
}
TEST(display_placement_centres_on_the_chosen_display_with_a_non_zero_origin) {
    auto r = rig();
    Placement p = centerOn(r[1], 1280, 720);                                   // the laptop panel sits at (1280, 256)
    CHECK(p.x == 1280 + 43 && p.y == 256 + 24);
    p = centerOn(r[0], 1280, 720);                                             // the CRT at 0,0
    CHECK(p.x == 0 && p.y == 152);
    p = centerOn(r[0], 1280, 1024);
    CHECK(p.x == 0 && p.y == 0);
    p = centerOn(r[1], 1920, 1080);                                            // bigger than the display: its origin, not half off it
    CHECK(p.x == 1280 && p.y == 256);
    DisplayInfo neg = disp(2, "left", -1920, -200, 1920, 1080, 60);            // a display to the LEFT of / above the primary has negative coordinates
    p = centerOn(neg, 1280, 720);
    CHECK(p.x == -1920 + 320 && p.y == -200 + 180);
    Res fit = fitWindow(r[0], {1920, 1080, 0});
    CHECK(fit.w == 1280 && fit.h == 1024);
    fit = fitWindow(r[0], {800, 600, 0});
    CHECK(fit.w == 800 && fit.h == 600);
}
TEST(display_fake_list_parsing_and_layout) {
    auto f = parseFakeDisplays("1366x768;1280x1024");
    CHECK_EQ(f.size(), (size_t)2);
    CHECK(f[0].index == 0 && f[0].x == 0 && f[0].w == 1366 && f[0].fake);
    CHECK(f[1].index == 1 && f[1].x == 1366 && f[1].w == 1280 && f[1].h == 1024);   // side by side
    CHECK(classifyAspect(f[1].w, f[1].h) == Aspect::R5_4 && isRetro(f[1]));
    CHECK(!f[0].modes.empty() && f[1].modes.back().w == 1280);                        // the native mode is offered
    for (const Mode& m : f[0].modes) CHECK(m.w <= 1366 && m.h <= 768);
    CHECK_EQ(parseFakeDisplays("800x600@75")[0].refreshHz, 75);
    CHECK_EQ(parseFakeDisplays("junk;640x480;;").size(), (size_t)1);                  // bad items are skipped, good ones kept
    CHECK(parseFakeDisplays("").empty());
    CHECK(parseFakeDisplays("nonsense").empty());
    std::string text = describeDisplay(f[1], true);
    CHECK(text.find("1280x1024") != std::string::npos && text.find("5:4") != std::string::npos && text.find("retro") != std::string::npos && text.find("chosen") != std::string::npos);
}

// ---- Hor+ field of view ----
TEST(fov_horplus_is_the_identity_at_16_9) {
    const float a = 16.0f / 9.0f;
    for (float base : {60.0f, 90.0f, 110.0f})
        CHECK(nearf(core::effectiveVerticalFov(core::FovMode::HorPlus, base, a), base, 1e-2));
    CHECK(nearf(core::effectiveVerticalFov(core::FovMode::HorPlus, 90.0f, 1366.0f / 768.0f), 90.0f, 0.05));   // the laptop panel: unchanged
}
TEST(fov_horplus_keeps_the_horizontal_view_on_narrow_screens) {
    const float ref = core::horizontalFov(90.0f, 16.0f / 9.0f);                                            // ~121.9 degrees
    for (float aspect : {1.6f, 4.0f / 3.0f, 5.0f / 4.0f}) {
        float v = core::effectiveVerticalFov(core::FovMode::HorPlus, 90.0f, aspect);
        CHECK(v > 90.0f);                                                                                   // more vertical view: nothing is cropped or zoomed in
        CHECK(nearf(core::horizontalFov(v, aspect), ref, 0.05));                                            // the horizontal extent is the 16:9 view's
    }
    CHECK(nearf(core::effectiveVerticalFov(core::FovMode::HorPlus, 90.0f, 1.25f), 109.8, 0.3));            // the numbers for the docs: 5:4
    CHECK(nearf(core::effectiveVerticalFov(core::FovMode::HorPlus, 90.0f, 4.0f / 3.0f), 106.3, 0.3));       // 4:3
    // vertical mode is exactly the old behaviour
    CHECK(nearf(core::effectiveVerticalFov(core::FovMode::Vertical, 90.0f, 1.25f), 90.0f));
    CHECK(nearf(core::effectiveVerticalFov(core::FovMode::Vertical, 90.0f, 3.5f), 90.0f));
}
TEST(fov_horplus_caps_the_horizontal_view_on_ultrawide) {
    CHECK(nearf(core::effectiveVerticalFov(core::FovMode::HorPlus, 90.0f, 21.0f / 9.0f), 90.0f, 0.05));    // 21:9: 133 degrees horizontal, still fine: vertical held
    float v = core::effectiveVerticalFov(core::FovMode::HorPlus, 90.0f, 32.0f / 9.0f);                     // 32:9 would be 148 degrees
    CHECK(v < 90.0f);
    CHECK(nearf(core::horizontalFov(v, 32.0f / 9.0f), core::kMaxHorizontalDeg, 0.1));
    for (float base : {30.0f, 90.0f, 140.0f})
        for (float aspect : {0.5f, 1.0f, 1.25f, 1.7778f, 2.4f, 3.6f, 8.0f}) {
            float e = core::effectiveVerticalFov(core::FovMode::HorPlus, base, aspect);
            CHECK(e >= 30.0f && e <= 140.0f && std::isfinite(e));                                            // always inside the setting's range
        }
    CHECK(nearf(core::effectiveVerticalFov(core::FovMode::HorPlus, 90.0f, 0.0f), 90.0f));                    // no aspect yet (minimised window): the setting
    CHECK(nearf(core::effectiveVerticalFov(core::FovMode::HorPlus, 500.0f, 16.0f / 9.0f), 140.0f));          // a wild setting is clamped
    CHECK(core::parseFovMode("vertical") == core::FovMode::Vertical && core::parseFovMode("horplus") == core::FovMode::HorPlus && core::parseFovMode("nonsense") == core::FovMode::HorPlus);
}

// ---- UI scale ----
TEST(ui_layout_scale_follows_the_smaller_dimension) {
    CHECK(nearf(core::uiLayoutScale(1280, 720), 1.0));
    CHECK(nearf(core::uiLayoutScale(1280, 1024), 1.0));                        // 5:4: the width limits
    CHECK(nearf(core::uiLayoutScale(1920, 1080), 1.5));
    CHECK(nearf(core::uiLayoutScale(800, 600), 800.0 / 1280.0, 1e-3));
    CHECK(nearf(core::uiLayoutScale(640, 480), 0.6));                          // clamped: 0.5 would be unreadable
    CHECK(nearf(core::uiLayoutScale(3440, 1440), 2.0));                        // ultrawide: the height limits
    CHECK(nearf(core::uiLayoutScale(8000, 8000), 3.0));
    CHECK(nearf(core::uiLayoutScale(0, 0), 0.6));                              // a minimised window: no zero or negative scale
}
