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

TEST(hud_overlay_mode_parsing) {
    CHECK(parseOverlayMode("minimal").mode == OverlayMode::Minimal && !parseOverlayMode("minimal").unknown);
    CHECK(parseOverlayMode("full").mode == OverlayMode::Full && !parseOverlayMode("full").unknown);
    CHECK(parseOverlayMode("hidden").mode == OverlayMode::Hidden && !parseOverlayMode("hidden").unknown);
    CHECK(parseOverlayMode("bogus").mode == OverlayMode::Minimal && parseOverlayMode("bogus").unknown);
    CHECK(parseOverlayMode("").unknown);
}

TEST(hud_overlay_plan_all_modes) {
    // default UI shown (chase view / no screens / no cockpit): everything, whatever the mode
    for (auto m : {OverlayMode::Minimal, OverlayMode::Full, OverlayMode::Hidden}) {
        OverlayPlan p = overlayPlan(m, true);
        CHECK(p.speed && p.bars && p.crosshair && p.banners && p.vignette && p.hint && p.destroyed);
    }
    OverlayPlan full = overlayPlan(OverlayMode::Full, false);
    CHECK(full.speed && full.bars && full.crosshair && full.banners && full.vignette && full.hint && full.destroyed);
    OverlayPlan mn = overlayPlan(OverlayMode::Minimal, false);
    CHECK(!mn.speed && !mn.bars);
    CHECK(mn.crosshair && mn.banners && mn.vignette && mn.hint && mn.destroyed);
    OverlayPlan hid = overlayPlan(OverlayMode::Hidden, false);
    CHECK(!hid.speed && !hid.bars && !hid.crosshair && !hid.banners && !hid.hint);
    CHECK(hid.vignette && hid.destroyed);
}

TEST(hud_hint_alpha_fade_edges) {
    CHECK_EQ(hintAlpha(0.0, 20.0f), 1.0f);
    CHECK_EQ(hintAlpha(19.9, 20.0f), 1.0f);
    CHECK_EQ(hintAlpha(20.0, 20.0f), 1.0f);            // exactly at N: still full
    CHECK_EQ(hintAlpha(21.0, 20.0f), 0.5f);            // N+1: halfway
    CHECK_EQ(hintAlpha(22.0, 20.0f), 0.0f);            // N+2: gone
    CHECK_EQ(hintAlpha(500.0, 20.0f), 0.0f);
    CHECK_EQ(hintAlpha(0.0, 0.0f), 1.0f);              // seconds = 0: never fades
    CHECK_EQ(hintAlpha(1e6, 0.0f), 1.0f);
}

TEST(hud_respawn_text_counts_3_2_1) {
    CHECK_EQ(respawnText(3.0f), std::string("RESPAWNING IN 3"));
    CHECK_EQ(respawnText(2.01f), std::string("RESPAWNING IN 3"));
    CHECK_EQ(respawnText(2.0f), std::string("RESPAWNING IN 2"));
    CHECK_EQ(respawnText(0.2f), std::string("RESPAWNING IN 1"));
    CHECK_EQ(respawnText(0.0f), std::string(""));
    CHECK_EQ(respawnText(-1.0f), std::string(""));
}

TEST(hud_orbit_status_text) {
    CHECK_EQ(orbitStatusText("Planet 1"), std::string("ORBIT LOCKED: PLANET 1"));
    CHECK_EQ(orbitStatusText("Sun"), std::string("ORBIT LOCKED: SUN"));
    CHECK_EQ(orbitStatusText(""), std::string("ORBIT LOCKED"));
}

TEST(hud_orbit_lock_state_and_released_banner) {
    HudState h;
    Snapshot s;
    CHECK(!h.orbitLocked());
    CHECK_EQ(h.orbitStatus(), std::string(""));
    h.onOrbitLock(true, "Planet 1", 10.0);
    CHECK(h.orbitLocked());
    CHECK_EQ(h.orbitStatus(), std::string("ORBIT LOCKED: PLANET 1"));
    CHECK(h.banners(s, 100.0).empty());                 // the status line is persistent, not a banner
    h.onOrbitLock(false, "Planet 1", 20.0);
    CHECK(!h.orbitLocked());
    CHECK_EQ(h.orbitStatus(), std::string(""));
    auto b = h.banners(s, 20.5);
    CHECK_EQ(b.size(), 1u);
    CHECK(b[0].kind == Warn::OrbitReleased);
    CHECK_EQ(b[0].text, std::string("ORBIT RELEASED"));
    CHECK(b[0].alpha > 0.0f && b[0].alpha <= 1.0f);
    CHECK(h.banners(s, 20.0 + kOrbitReleasedSeconds + 0.1).empty());   // expires
}

TEST(hud_orbit_release_without_lock_shows_nothing) {
    HudState h;
    Snapshot s;
    h.onOrbitLock(false, "Sun", 5.0);
    CHECK(h.banners(s, 5.1).empty());
    h.onOrbitLock(true, "Sun", 6.0);
    h.onOrbitLock(false, "Sun", 7.0);
    h.onOrbitLock(false, "Sun", 7.1);                   // duplicate release must not restart the banner
    CHECK(h.banners(s, 7.0 + kOrbitReleasedSeconds + 0.05).empty());
}

TEST(hud_orbit_banner_hidden_when_dead) {
    HudState h;
    Snapshot s; s.alive = false;
    h.onOrbitLock(true, "Sun", 1.0);
    h.onOrbitLock(false, "Sun", 2.0);
    CHECK(h.banners(s, 2.1).empty());
}

TEST(hud_distance_text_units) {
    CHECK_EQ(distanceText(420.4f), std::string("420 m"));
    CHECK_EQ(distanceText(999.0f), std::string("999 m"));
    CHECK_EQ(distanceText(1250.0f), std::string("1.2 km"));
}

TEST(hud_dock_reason_shortening) {
    CHECK_EQ(shortDockReason("too far from the station"), std::string("too far"));
    CHECK_EQ(shortDockReason("too fast"), std::string("too fast"));
    CHECK_EQ(shortDockReason("Too fast (30 m/s, max 15)"), std::string("too fast"));
    CHECK_EQ(shortDockReason("warp drive engaged"), std::string("warp drive on"));
    CHECK_EQ(shortDockReason("orbit lock engaged"), std::string("orbit lock on"));
    CHECK_EQ(shortDockReason("something new"), std::string("something new"));
}

TEST(hud_dock_prompt_range) {
    CHECK(std::fabs(dockPromptRange(120.0f, 0.0f) - 480.0f) < 1e-3f);      // 4 x dock radius
    CHECK(std::fabs(dockPromptRange(120.0f, 1000.0f) - 1000.0f) < 1e-3f);  // tunable wins
    CHECK(inDockPromptRange(479.0f, 120.0f, 0.0f));
    CHECK(!inDockPromptRange(481.0f, 120.0f, 0.0f));
    CHECK(inDockPromptRange(900.0f, 120.0f, 1000.0f));
}

TEST(hud_dock_prompt_states) {
    DockQuery q; q.has = true; q.distance = 420; q.name = "Station 1"; q.ok = false; q.reason = "too fast";
    DockPrompt p = dockPrompt(q, 120, 0, false);
    CHECK(p.show && !p.ready);
    CHECK_EQ(p.status, std::string("STATION 1  420 m"));
    CHECK_EQ(p.action, std::string("too fast"));
    q.ok = true; q.distance = 100;
    p = dockPrompt(q, 120, 0, false);
    CHECK(p.show && p.ready);
    CHECK_EQ(p.action, std::string("DOCK [G]"));
    q.distance = 900;                                       // out of range: nothing
    CHECK(!dockPrompt(q, 120, 0, false).show);
    q.distance = 100;
    CHECK(!dockPrompt(q, 120, 0, true).show);               // docked: the docked line replaces it
    q.has = false;
    CHECK(!dockPrompt(q, 120, 0, false).show);             // no station at all
}

TEST(hud_docked_state_lines_and_banners) {
    HudState h;
    Snapshot s;
    CHECK(!h.docked());
    CHECK_EQ(h.dockedStatus(), std::string(""));
    h.onDocked("Station 1", 10.0);
    CHECK(h.docked());
    CHECK_EQ(h.dockedStatus(), std::string("DOCKED: STATION 1 - [G] UNDOCK"));
    auto b = h.banners(s, 10.5);
    CHECK_EQ(b.size(), 1u);
    CHECK(b[0].kind == Warn::Docked);
    CHECK_EQ(b[0].text, std::string("DOCKED"));
    CHECK(h.banners(s, 10.0 + kDockBannerSeconds + 0.1).empty());
    h.onUndocked(20.0);
    CHECK(!h.docked());
    b = h.banners(s, 20.5);
    CHECK_EQ(b.size(), 1u);
    CHECK(b[0].kind == Warn::Undocked);
    CHECK_EQ(b[0].text, std::string("UNDOCKED"));
}

TEST(hud_undock_without_dock_shows_nothing) {
    HudState h;
    Snapshot s;
    h.onUndocked(5.0);
    CHECK(h.banners(s, 5.1).empty());
}

TEST(hud_station_names_are_shortened) {
    CHECK_EQ(shortStationName("Station 1 (Planet 1, orbital)"), std::string("Station 1"));
    CHECK_EQ(shortStationName("Station 2"), std::string("Station 2"));
    CHECK_EQ(shortStationName(""), std::string(""));
    DockQuery q; q.has = true; q.ok = true; q.distance = 88; q.name = "Station 1 (Planet 1, orbital)";
    CHECK_EQ(dockPrompt(q, 120, 0, false).status, std::string("STATION 1  88 m"));
    CHECK_EQ(dockedText("Station 1 (Planet 1, orbital)"), std::string("DOCKED: STATION 1 - [G] UNDOCK"));
}

TEST(hud_dock_banners_replace_each_other) {
    HudState h;
    Snapshot s;
    h.onDocked("Station 1", 1.0);
    h.onUndocked(1.5);
    auto b = h.banners(s, 1.6);
    CHECK_EQ(b.size(), 1u);
    CHECK(b[0].kind == Warn::Undocked);
}

TEST(hud_heat_colour_ramp) {
    RGB cool = heatColor(0.0f), warm = heatColor(0.6f), hot = heatColor(1.0f);
    CHECK(cool.b > cool.r);                              // cyan
    CHECK(warm.r > 0.9f && warm.g > 0.6f && warm.b < 0.4f);   // amber
    CHECK(hot.r > 0.9f && hot.g < 0.3f);                 // red
    CHECK(std::fabs(heatColor(5.0f).g - hot.g) < 1e-4f); // clamped
}

TEST(hud_heat_segments) {
    CHECK_EQ(heatSegments(0.0f), 0);
    CHECK_EQ(heatSegments(0.01f), 1);
    CHECK_EQ(heatSegments(0.5f), 4);
    CHECK_EQ(heatSegments(1.0f), kHeatSegments);
    CHECK_EQ(heatSegments(2.0f), kHeatSegments);
    CHECK_EQ(heatSegments(-1.0f), 0);
    CHECK_EQ(heatSegments(0.5f, 10), 5);
}

TEST(hud_overheat_flash_stays_visible) {
    for (double t = 0; t < 3.0; t += 0.05) { float f = overheatFlash(t); CHECK(f >= 0.3f && f <= 1.01f); }
}

TEST(hud_hit_marker_fade) {
    HitMarker none = hitMarker(5.0f, -1.0f);
    CHECK(none.alpha <= 0.0f);
    HitMarker fresh = hitMarker(0.0f, -1.0f);
    CHECK(fresh.alpha > 0.99f && !fresh.kill);
    HitMarker half = hitMarker(kHitMarkerSeconds / 2, -1.0f);
    CHECK(half.alpha > 0.45f && half.alpha < 0.55f);
    CHECK(hitMarker(kHitMarkerSeconds + 0.01f, -1.0f).alpha <= 0.0f);
    HitMarker kill = hitMarker(0.3f, 0.1f);              // the plain hit has faded, the kill flash has not
    CHECK(kill.kill && kill.alpha > 0.5f);
    CHECK(hitMarker(0.0f, kKillMarkerSeconds + 0.1f).alpha > 0.99f);   // only the plain hit remains
}

TEST(hud_weapon_labels_and_row_state) {
    CHECK_EQ(weaponLabel("Blaster", 0), std::string("BLASTER  [1]"));
    CHECK_EQ(weaponLabel("Mining Beam", 1), std::string("MINING BEAM  [2]"));
    CHECK(weaponRowState(false, false, 0.1f) == WeaponRow::Ready);
    CHECK(weaponRowState(false, false, 0.8f) == WeaponRow::Hot);
    CHECK(weaponRowState(false, true, 1.0f) == WeaponRow::Locked);
    CHECK(weaponRowState(true, true, 1.0f) == WeaponRow::Docked);   // docked wins
    CHECK_EQ(std::string(weaponRowNote(WeaponRow::Locked)), std::string("OVERHEATED"));
    CHECK_EQ(std::string(weaponRowNote(WeaponRow::Docked)), std::string("DOCKED"));
    CHECK_EQ(std::string(weaponRowNote(WeaponRow::Ready)), std::string(""));
}

TEST(hud_weapon_banners) {
    HudState h;
    Snapshot s;
    h.onOverheated("Blaster", 10.0);
    h.onWeaponChanged("Mining Beam", 10.0);
    auto b = h.banners(s, 10.3);
    CHECK_EQ(b.size(), 2u);
    bool sawHeat = false, sawChange = false;
    for (auto& x : b) {
        if (x.kind == Warn::WeaponOverheated) { sawHeat = true; CHECK_EQ(x.text, std::string("BLASTER OVERHEATED")); }
        if (x.kind == Warn::WeaponChanged) { sawChange = true; CHECK_EQ(x.text, std::string("WEAPON: MINING BEAM")); }
    }
    CHECK(sawHeat && sawChange);
    CHECK(h.banners(s, 10.0 + 5.0).empty());              // both expired
    s.alive = false;
    CHECK(h.banners(s, 10.1).empty());                    // dead: none
}

TEST(hud_kill_age) {
    HudState h;
    CHECK(h.killAge(5.0) < 0);                            // never
    h.onEnemyKilled(5.0);
    CHECK(std::fabs(h.killAge(5.2) - 0.2f) < 1e-4f);
}

TEST(hud_cargo_fraction_colour_text) {
    CHECK(std::fabs(cargoFraction(45, 100) - 0.45f) < 1e-5f);
    CHECK(cargoFraction(150, 100) <= 1.0f);
    CHECK(cargoFraction(5, 0) <= 0.0f);
    RGB calm = cargoColor(0.5f), amber = cargoColor(0.9f), red = cargoColor(1.0f);
    CHECK(calm.b > calm.r);                         // cyan
    CHECK(amber.r > 0.9f && amber.g > 0.6f && amber.b < 0.4f);
    CHECK(red.r > 0.9f && red.g < 0.3f);
    CHECK(cargoColor(0.8f).b > 0.9f);               // 80% is still calm, above it is amber
    CHECK_EQ(cargoText(45, 100), std::string("CARGO 45 / 100"));
    CHECK_EQ(cargoText(0, 800), std::string("CARGO 0 / 800"));
}

TEST(hud_pickup_text_and_labels) {
    CHECK_EQ(pickupText(6, "CRYSTAL"), std::string("+6 CRYSTAL"));
    CHECK_EQ(oreLabel("crystal", "Crystal"), std::string("CRYSTAL"));
    CHECK_EQ(oreLabel("mystery", ""), std::string("MYSTERY"));
}

TEST(hud_pickups_merge_within_the_window) {
    HudState h;
    RGB c{0.5f, 0.5f, 0.5f};
    CHECK_EQ(h.pickupCount(1.0), 0);
    h.onOreMined("iron", "IRON", c, 3, 1.0);
    h.onOreMined("iron", "IRON", c, 4, 1.3);        // within 0.5 s: merged
    CHECK_EQ(h.pickupCount(1.4), 1);
    std::string text; float alpha = 0;
    h.forEachPickup(1.4, [&](const std::string& t, const RGB&, float a) { text = t; alpha = a; });
    CHECK_EQ(text, std::string("+7 IRON"));
    CHECK(alpha > 0.0f && alpha <= 1.0f);
    h.onOreMined("iron", "IRON", c, 2, 2.0);        // 0.7 s after the last one: a new banner
    CHECK_EQ(h.pickupCount(2.0), 2);
    h.onOreMined("copper", "COPPER", c, 1, 2.0);    // another ore: separate
    CHECK_EQ(h.pickupCount(2.0), 3);
}

TEST(hud_pickups_expire_and_fade) {
    HudState h;
    RGB c;
    h.onOreMined("gold", "GOLD", c, 5, 10.0);
    float a0 = 0, a1 = 0;
    h.forEachPickup(10.0, [&](const std::string&, const RGB&, float a) { a0 = a; });
    h.forEachPickup(10.0 + kPickupSeconds * 0.9, [&](const std::string&, const RGB&, float a) { a1 = a; });
    CHECK(a0 > 0.99f);
    CHECK(a1 > 0.0f && a1 < a0);
    CHECK_EQ(h.pickupCount(10.0 + kPickupSeconds + 0.01), 0);
    h.onOreMined("gold", "GOLD", c, 0, 20.0);       // nothing fitted: no banner
    CHECK_EQ(h.pickupCount(20.0), 0);
}

TEST(hud_pickup_slots_are_reused_when_full) {
    HudState h;
    RGB c;
    for (int i = 0; i < kMaxPickups + 3; i++) h.onOreMined("ore" + std::to_string(i), "X", c, 1, 1.0 + i * 0.01);
    CHECK_EQ(h.pickupCount(1.1), kMaxPickups);       // the oldest were replaced, no growth
}

TEST(hud_cargo_full_banner_is_rate_limited) {
    HudState h;
    Snapshot s;
    h.onCargoFull(10.0);
    auto b = h.banners(s, 10.1);
    CHECK_EQ(b.size(), 1u);
    CHECK(b[0].kind == Warn::CargoFull);
    CHECK_EQ(b[0].text, std::string("CARGO FULL"));
    h.onCargoFull(10.5);                              // too soon: ignored, does not extend it
    h.onCargoFull(11.0);
    CHECK(h.banners(s, 10.0 + kCargoFullSeconds + 0.05).empty());
    h.onCargoFull(12.5);                              // after the gap: shows again
    CHECK_EQ(h.banners(s, 12.6).size(), 1u);
}
