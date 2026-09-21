// The Ore Scanner presentation rules (combat/weapons/scanner_rules.h) and the radar dot helper (ship/cockpit/radar_map.h).
#include <cmath>
#include "combat/weapons/scanner_rules.h"
#include "gameplay/mining/mining_rules.h"
#include "ship/cockpit/radar_map.h"
#include "tests/test.h"

namespace {
bool nearScan(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
}

TEST(scanner_lock_text_without_perk_hides_ore_and_yield) {
    CHECK_EQ(combat::lockInfoText(false, "IRON", 9.6f, 850.0f, 0.5f), std::string("ASTEROID  r 9.6  850 m"));
    CHECK_EQ(combat::lockInfoText(false, "", 3.04f, 1234.4f, 0.5f), std::string("ASTEROID  r 3.0  1234 m"));
}

TEST(scanner_lock_text_with_perk_shows_ore_and_yield) {
    CHECK_EQ(combat::lockInfoText(true, "IRON", 9.6f, 850.0f, 0.5f), std::string("ASTEROID  IRON  r 9.6  ~46 ore  850 m"));
    CHECK_EQ(combat::lockInfoText(true, "", 9.0f, 850.0f, 0.5f), std::string("ASTEROID  r 9.0  ~41 ore  850 m"));
}

TEST(scanner_expected_yield_matches_mining) {
    CHECK_EQ(combat::expectedYield(9.0f, 0.5f), 41);        // 40.5 rounds up
    CHECK_EQ(combat::expectedYield(3.0f, 0.5f), 5);         // 4.5 -> 5
    CHECK_EQ(combat::expectedYield(0.5f, 0.5f), 1);         // at least 1
    CHECK_EQ(combat::expectedYield(4.0f, 0.0f), 1);
    for (float r : {0.7f, 2.2f, 5.5f, 9.6f, 12.3f})
        for (float k : {0.25f, 0.5f, 1.0f}) CHECK_EQ(combat::expectedYield(r, k), gameplay::totalYield(r, k));   // same formula as gameplay/mining
}

TEST(scanner_distance_bucket_and_names) {
    CHECK_EQ(combat::lockDistanceBucket(854.0f), 85);
    CHECK_EQ(combat::lockDistanceBucket(856.0f), 86);
    CHECK_EQ(combat::lockDistanceBucket(-5.0f), 0);
    CHECK_EQ(combat::oreDisplayName("iron", "Iron"), std::string("IRON"));
    CHECK_EQ(combat::oreDisplayName("platinum", ""), std::string("PLATINUM"));
}

TEST(scanner_ore_look_colour_and_tier) {
    const float iron[3] = {0.40f, 0.35f, 0.30f}, gold[3] = {0.80f, 0.70f, 0.20f};
    combat::OreLook a = combat::oreLook(true, true, iron, 40);
    CHECK(nearScan(a.r, 0.75f) && nearScan(a.g, 0.35f * 0.75f / 0.40f) && nearScan(a.b, 0.30f * 0.75f / 0.40f));   // brightened, hue kept
    CHECK_EQ(a.tierBonus, 0);
    combat::OreLook g = combat::oreLook(true, true, gold, 8);
    CHECK(nearScan(g.r, 0.80f) && nearScan(g.g, 0.70f));    // already bright enough: unchanged
    CHECK_EQ(g.tierBonus, 1);
    CHECK_EQ(combat::oreLook(true, true, gold, 9).tierBonus, 0);
    combat::OreLook none = combat::oreLook(false, true, gold, 8);   // no scanner: the tan look, no bonus
    CHECK(nearScan(none.r, 0.5f) && nearScan(none.g, 0.45f) && nearScan(none.b, 0.35f) && none.tierBonus == 0);
    combat::OreLook unk = combat::oreLook(true, false, gold, 8);    // unknown ore
    CHECK(nearScan(unk.r, 0.5f) && unk.tierBonus == 0);
    const float black[3] = {0, 0, 0};
    CHECK(nearScan(combat::oreLook(true, true, black, 1).r, 0.5f));
}

TEST(scanner_radar_rock_dot) {
    const float uranium[3] = {0.30f, 0.80f, 0.30f}, iron[3] = {0.40f, 0.35f, 0.30f};
    for (int t = 0; t <= 2; t++) {                           // no perk: exactly the old dim tan dot at its own tier
        cockpit::RockDot d = cockpit::radarRockDot(t, false, true, uranium, 5);
        CHECK(d.tier == t && nearScan(d.r, 0.5f) && nearScan(d.g, 0.45f) && nearScan(d.b, 0.35f));
    }
    cockpit::RockDot u = cockpit::radarRockDot(0, true, true, uranium, 5);
    CHECK(u.tier == 1 && nearScan(u.g, 0.80f) && nearScan(u.r, 0.30f));
    CHECK_EQ(cockpit::radarRockDot(2, true, true, uranium, 5).tier, 2);   // capped at the largest tier
    CHECK_EQ(cockpit::radarRockDot(1, true, true, iron, 40).tier, 1);
    CHECK(cockpit::asteroidDotSize(cockpit::radarRockDot(0, true, true, uranium, 5).tier) > cockpit::asteroidDotSize(0));
}
