#include "ship/ship_core/ship_rules.h"
#include "tests/test.h"

using namespace ship::rules;

namespace {
Vitals shielded() { Vitals v; installShield(v, true); return v; }
bool near(float a, float b) { return a - b < 1e-3f && b - a < 1e-3f; }
} // namespace

TEST(ship_damage_goes_to_hull_without_shield) {
    Vitals v;
    auto r = applyDamage(v, 30);
    CHECK(near(v.hp, 70)); CHECK(near(r.toHull, 30)); CHECK(near(r.absorbed, 0)); CHECK(!r.shieldBroken && !r.died);
}

TEST(ship_shield_absorbs_first_then_hull) {
    Vitals v = shielded();
    auto r = applyDamage(v, 50);
    CHECK(near(v.shield, 150)); CHECK(near(v.hp, 100)); CHECK(near(r.absorbed, 50)); CHECK(near(r.toHull, 0));
    r = applyDamage(v, 170);                          // 150 absorbed, 20 spills to the hull, shield breaks
    CHECK(near(r.absorbed, 150)); CHECK(near(r.toHull, 20)); CHECK(near(v.hp, 80));
    CHECK(r.shieldBroken);
    CHECK(near(v.shield, 0)); CHECK(!v.shieldInstalled); CHECK(!v.shieldEnabled);
    r = applyDamage(v, 10);                           // gone for good: hull takes it all
    CHECK(near(r.toHull, 10)); CHECK(!r.shieldBroken);
}

TEST(ship_shield_exact_break_keeps_installed_until_spill) {
    Vitals v = shielded();
    auto r = applyDamage(v, 200);                     // exactly the shield: absorbed, not "broken" (0 left, still installed)
    CHECK(near(r.absorbed, 200)); CHECK(!r.shieldBroken); CHECK(near(v.shield, 0));
    r = applyDamage(v, 5);                            // shield empty: disabled shield does not absorb; hull takes it
    CHECK(near(r.toHull, 5));
}

TEST(ship_disabled_shield_does_not_absorb) {
    Vitals v = shielded();
    v.shieldEnabled = false;
    auto r = applyDamage(v, 10);
    CHECK(near(r.toHull, 10)); CHECK(near(v.shield, 200));
}

TEST(ship_death_at_zero_hp_and_ignores_further_damage) {
    Vitals v;
    auto r = applyDamage(v, 100);
    CHECK(r.died); CHECK(!v.alive); CHECK(near(v.hp, 0));
    r = applyDamage(v, 50);
    CHECK(!r.died); CHECK(near(r.toHull, 0));
    Vitals w;
    applyDamage(w, 500);                              // overkill clamps to 0
    CHECK(near(w.hp, 0)); CHECK(!w.alive);
}

TEST(ship_kill_is_instant_and_once) {
    Vitals v = shielded();
    CHECK(kill(v)); CHECK(!v.alive); CHECK(near(v.hp, 0));
    CHECK(!kill(v));
}

TEST(ship_regen_clamps_and_only_when_alive) {
    Vitals v; v.hp = 99.9f;
    regen(v, 0.2f, 1.0f);
    CHECK(near(v.hp, 100));                           // clamped to maxHp
    v.hp = 50;
    for (int i = 0; i < 300; i++) regen(v, 0.2f, 1.0f / 60);   // 5 s
    CHECK(near(v.hp, 51));
    Vitals d; applyDamage(d, 1000);
    regen(d, 0.2f, 10);
    CHECK(near(d.hp, 0)); CHECK(!d.alive);
}

TEST(ship_heal_clamps_and_ignored_when_dead) {
    Vitals v; v.hp = 90;
    heal(v, 50); CHECK(near(v.hp, 100));
    Vitals d; applyDamage(d, 1000); heal(d, 10);
    CHECK(near(d.hp, 0)); CHECK(!d.alive);
}

TEST(ship_fuel_add_and_consume_clamp) {
    Vitals v;
    bool emptied = false;
    CHECK(consumeWarpFuel(v, 30, emptied)); CHECK(near(v.warpFuel, 70)); CHECK(!emptied);
    addWarpFuel(v, 500); CHECK(near(v.warpFuel, 100));
    CHECK(consumeWarpFuel(v, 60, emptied)); CHECK(near(v.warpFuel, 40));
    CHECK(!consumeWarpFuel(v, 41, emptied)); CHECK(near(v.warpFuel, 40)); CHECK(!emptied);   // not enough: nothing taken
    CHECK(consumeWarpFuel(v, 40, emptied)); CHECK(near(v.warpFuel, 0)); CHECK(emptied);      // hit 0 from >0
    CHECK(consumeWarpFuel(v, 0, emptied)); CHECK(!emptied);                                  // already empty: no second event
    addWarpFuel(v, -5); CHECK(near(v.warpFuel, 0));
}

TEST(ship_max_hp_is_capped) {
    Vitals v;
    for (int i = 0; i < 20; i++) addMaxHp(v, 10, 200);
    CHECK(near(v.maxHp, 200));
    CHECK(near(v.hp, 100));                           // hull is not topped up, like the old game
}

TEST(ship_collision_damage_formula) {
    CollisionParams p;
    CHECK(near(collisionDamage("asteroid", 3, 200, p), 10));      // small at full speed
    CHECK(near(collisionDamage("asteroid", 10, 100, p), 10));     // medium at half speed: 0.5 * 20
    CHECK(near(collisionDamage("asteroid", 40, 200, p), 35));     // big
    CHECK(near(collisionDamage("planet", 500, 200, p), 50));
    CHECK(near(collisionDamage("asteroid", 3, 2, p), 1));         // scratch: 0.1 rounds up to 1 HP
    CHECK(near(collisionDamage("asteroid", 3, 0, p), 1));
    CHECK(near(collisionDamage("asteroid", 3, 400, p), 20));      // faster than ref is worse
    CHECK(near(collisionDamage("crate", 1, 200, p), 10));         // unknown kind
}

TEST(ship_sun_hit_is_lethal_at_any_speed_unless_tunable_off) {
    CollisionParams p;                                   // sunKills default true
    CHECK(collisionDamage("sun", 1500, 0.5f, p) >= 9999.0f);
    Vitals v = shielded();                               // even a full shield cannot absorb it
    auto r = applyDamage(v, collisionDamage("sun", 1500, 1.0f, p));
    CHECK(r.died); CHECK(!v.alive); CHECK(r.shieldBroken);
    p.sunKills = false;                                  // tunable off: tiered like a planet
    CHECK(near(collisionDamage("sun", 1500, 200, p), 50));
    CHECK(near(collisionDamage("sun", 1500, 100, p), 25));
    CHECK(near(collisionDamage("sun", 1500, 1, p), 1));   // scratch minimum
}

TEST(ship_planet_and_moon_use_the_planet_tier) {
    CollisionParams p;
    CHECK(near(collisionDamage("planet", 400, 100, p), 25));
    CHECK(near(collisionDamage("moon", 60, 100, p), 25));
    CHECK(near(collisionDamage("planet", 400, 1, p), 1));
}
