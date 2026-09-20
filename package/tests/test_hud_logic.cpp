#include "tests/test.h"
#include "ui/ship_hud/hud_logic.h"

using namespace hud;

TEST(hud_fraction_clamps_and_guards_zero_max) {
    CHECK_EQ(fraction(50, 100), 0.5f);
    CHECK_EQ(fraction(150, 100), 1.0f);
    CHECK_EQ(fraction(-5, 100), 0.0f);
    CHECK_EQ(fraction(10, 0), 0.0f);
}

TEST(hud_bar_colour_goes_green_amber_red) {
    RGB full = barColor(Bar::Hp, 1.0f), mid = barColor(Bar::Hp, 0.5f), low = barColor(Bar::Hp, 0.0f);
    CHECK(full.g > full.r);                 // green
    CHECK(mid.r > 0.9f && mid.g > 0.6f);    // amber
    CHECK(low.r > 0.9f && low.g < 0.3f);    // red
    RGB sh = barColor(Bar::Shield, 1.0f);
    CHECK(sh.b > sh.r);                     // shield is blue when full
    CHECK_EQ(barColor(Bar::Hp, 2.0f).g, full.g);   // out of range is clamped
}

TEST(hud_layout_scales_and_never_overlaps_hint) {
    const int sizes[][2] = {{1024, 600}, {1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}, {800, 600}};
    for (auto& sz : sizes)
        for (int rows = 2; rows <= 3; rows++) {
            Layout L = computeLayout(sz[0], sz[1], rows);
            CHECK(L.bars.bottom() < L.hint.y);
            CHECK(L.hint.bottom() <= sz[1]);
        }
    CHECK(computeLayout(3840, 2160, 3).scale > computeLayout(1280, 720, 3).scale * 2.5f);
    CHECK_EQ(computeLayout(1280, 720, 3).scale, 1.0f);
}

TEST(hud_hint_shrinks_to_fit_narrow_screens) {
    auto tw = [](const std::string& t, int size) { return (int)t.size() * size / 2; };   // fake metric
    std::string hint(100, 'x');
    Layout L = computeLayout(1024, 600, 3);
    int f = fitHint(L, 1024, tw, hint);
    CHECK(L.hint.x >= 0);
    CHECK(L.hint.right() <= 1024);
    CHECK(f <= (int)L.hintFont);
}

TEST(hud_low_hp_and_low_fuel_thresholds) {
    HudState h;
    Snapshot s;
    CHECK(h.banners(s, 1.0).empty());
    s.hp = 25;   // exactly 25% counts
    auto b = h.banners(s, 1.0);
    CHECK_EQ(b.size(), 1u);
    CHECK(b[0].kind == Warn::LowHp);
    s.hp = 26; s.warpFuel = 15;
    b = h.banners(s, 1.0);
    CHECK_EQ(b.size(), 1u);
    CHECK(b[0].kind == Warn::LowFuel);
    s.warpFuel = 16;
    CHECK(h.banners(s, 1.0).empty());
}

TEST(hud_event_banners_expire_and_respawn_clears) {
    HudState h;
    Snapshot s;
    h.onShieldBroken(10.0);
    h.onFuelEmpty(10.0);
    CHECK_EQ(h.banners(s, 10.5).size(), 2u);
    CHECK(h.banners(s, 13.5).empty());
    h.onShieldBroken(20.0);
    h.onRespawned();
    CHECK(h.banners(s, 20.1).empty());
}

TEST(hud_empty_fuel_event_replaces_low_fuel_banner) {
    HudState h;
    Snapshot s; s.warpFuel = 0;
    CHECK(h.banners(s, 1.0).empty());            // no event yet, and 0 is not "low" (event owns it)
    h.onFuelEmpty(1.0);
    auto b = h.banners(s, 1.5);
    CHECK_EQ(b.size(), 1u);
    CHECK(b[0].kind == Warn::FuelEmpty);
}

TEST(hud_dead_ship_shows_no_warning_banners) {
    HudState h;
    Snapshot s; s.hp = 0; s.alive = false;
    h.onShieldBroken(1.0);
    CHECK(h.banners(s, 1.1).empty());
}

TEST(hud_impact_fades_over_0_7_seconds) {
    HudState h;
    CHECK_EQ(h.impactAlpha(5.0), 0.0f);
    h.onDamage(12.0f, 0.0f, "asteroid", 5.0);
    CHECK(h.impactAlpha(5.0) > 0.99f);
    float mid = h.impactAlpha(5.35);
    CHECK(mid > 0.45f && mid < 0.55f);
    CHECK_EQ(h.impactAlpha(5.7), 0.0f);
    CHECK_EQ(h.impactAmount(), 12.0f);
    CHECK_EQ(impactText(12.4f, "asteroid"), std::string("IMPACT  12  ASTEROID"));
}

TEST(hud_shield_and_fuel_bars_keep_colour_then_warn) {
    RGB hi = barColor(Bar::Fuel, 1.0f), mid = barColor(Bar::Fuel, 0.5f), low = barColor(Bar::Fuel, 0.0f);
    CHECK_EQ(hi.r, mid.r);
    CHECK(low.r > 0.9f && low.g < 0.3f);
    RGB amber = barColor(Bar::Shield, 0.15f);
    CHECK(amber.r > 0.9f && amber.g > 0.6f);
}

TEST(hud_impact_total_includes_shield_absorption) {
    HudState h;
    h.onDamage(0.0f, 10.0f, "asteroid", 1.0);           // all absorbed
    CHECK_EQ(h.impactAmount(), 10.0f);
    CHECK(h.impactShielded());
    CHECK_EQ(impactText(h.impactAmount(), h.impactSource(), h.impactShielded()), std::string("IMPACT  10  ASTEROID  (SHIELD)"));
    h.onDamage(4.0f, 6.0f, "rock", 2.0);                // partly absorbed
    CHECK_EQ(h.impactAmount(), 10.0f);
    CHECK(h.impactShielded());
    CHECK_EQ(impactText(h.impactAmount(), h.impactSource(), h.impactShielded()), std::string("IMPACT  10  ROCK  (SHIELD)"));
    h.onDamage(10.0f, 0.0f, "asteroid", 3.0);           // none absorbed
    CHECK_EQ(h.impactAmount(), 10.0f);
    CHECK(!h.impactShielded());
    CHECK_EQ(impactText(h.impactAmount(), h.impactSource(), h.impactShielded()), std::string("IMPACT  10  ASTEROID"));
}

TEST(hud_impact_severity_scales_with_total) {
    HudState h;
    h.onDamage(0.0f, 5.0f, "a", 1.0);
    float small = h.impactSeverity();
    h.onDamage(30.0f, 30.0f, "a", 2.0);
    CHECK(h.impactSeverity() > small);
    CHECK_EQ(h.impactSeverity(), 1.0f);
}
