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

// ---------------- contact damage: scrape threshold, cooldown, resting contact ----------------
namespace {
using ship::rules::CollisionParams;
using ship::rules::ContactCooldown;
bool nearv(const engine::Vec3& a, const engine::Vec3& b, float e = 1e-4f) { return std::fabs(a.x - b.x) < e && std::fabs(a.y - b.y) < e && std::fabs(a.z - b.z) < e; }

// One ship pressed against a static sphere at the origin with thrust held toward it. Mirrors ship_core: thrust, move, physics reports a Collided when the
// hull goes from outside to inside the sphere (start touching), pushout, velocity response, damage rules. `oldRules` = the behaviour before the fix
// (bounce always, every event at least minDamage, no cooldown).
struct Rest {
    float hp = 100, hits = 0;
    int events = 0;
    float maxIntoSpeed = 0;
};
Rest holdThrustAgainstSphere(bool oldRules, float seconds) {
    const float dt = 1.0f / 60, R = 60.0f, hull = 2.5f, thrust = 40.0f, bounce = 0.35f;
    CollisionParams col;
    if (oldRules) { col.minSpeed = 0.0f; col.cooldown = 0.0f; }
    ContactCooldown cd;
    engine::Vec3 pos{0, 0, R + hull + 0.02f}, vel{0, 0, 0};           // resting on the surface, thrust pushes toward the origin (-Z)
    Rest r;
    const engine::Vec3 n{0, 0, -1};                                    // ship -> sphere
    for (int step = 0; step < (int)(seconds / dt); step++) {
        cd.tick(dt);
        bool wasOutside = engine::length(pos) >= R + hull;
        vel += engine::Vec3{0, 0, -thrust * dt};                         // held thrust
        pos += vel * dt;
        if (wasOutside && engine::length(pos) < R + hull) {              // physics: they START touching
            r.events++;
            float closing = std::max(0.0f, engine::dot(vel, n));
            r.maxIntoSpeed = std::max(r.maxIntoSpeed, closing);
            pos = engine::normalize(pos) * (R + hull + 0.02f);
            if (oldRules) { if (closing > 0) vel -= n * ((1.0f + bounce) * closing); }
            else vel = ship::rules::velocityAfterContact(vel, n, closing, bounce, col.minSpeed);
            float dmg = ship::rules::contactDamage("planet", R, closing, col);
            if (oldRules) dmg = ship::rules::collisionDamage("planet", R, closing, col);
            if (dmg > 0 && (oldRules || cd.ready(7))) { r.hp -= dmg; r.hits++; cd.arm(7, col.cooldown); }
        }
    }
    return r;
}
}

TEST(contact_damage_scrape_threshold_edges) {
    CollisionParams p;                                                                   // minSpeed 3
    CHECK(std::fabs(ship::rules::contactDamage("asteroid", 2.0f, 0.0f, p)) < 1e-6f);
    CHECK(std::fabs(ship::rules::contactDamage("asteroid", 2.0f, 2.99f, p)) < 1e-6f);   // just below: a scrape
    CHECK(ship::rules::contactDamage("asteroid", 2.0f, 3.0f, p) >= p.minDamage);        // at the threshold: the old formula, at least 1 HP
    CHECK(std::fabs(ship::rules::contactDamage("asteroid", 2.0f, 3.0f, p) - ship::rules::collisionDamage("asteroid", 2.0f, 3.0f, p)) < 1e-6f);
    CHECK(std::fabs(ship::rules::contactDamage("planet", 400.0f, std::nanf(""), p)) < 1e-6f);   // garbage speed is no damage
    CHECK(std::fabs(ship::rules::contactDamage("planet", 400.0f, -5.0f, p)) < 1e-6f);
    CHECK(std::fabs(ship::rules::contactDamage("sun", 1000.0f, 0.0f, p) - p.sun) < 1e-3f);      // the sun kills at any speed
    CHECK(std::fabs(ship::rules::contactDamage("sun", 1000.0f, 1.0f, p) - p.sun) < 1e-3f);
    p.sunKills = false;
    CHECK(std::fabs(ship::rules::contactDamage("sun", 1000.0f, 1.0f, p)) < 1e-6f);              // a non-lethal sun scrapes like anything else
    CollisionParams q;
    q.minSpeed = 10.0f;                                                                  // the threshold is a tunable
    CHECK(std::fabs(ship::rules::contactDamage("asteroid", 2.0f, 9.9f, q)) < 1e-6f && ship::rules::contactDamage("asteroid", 2.0f, 10.0f, q) >= 1.0f);
}
TEST(contact_damage_real_impacts_hurt_exactly_as_before) {
    CollisionParams p;
    struct Case { const char* kind; float radius, speed; };
    for (Case c : {Case{"asteroid", 2.0f, 100.0f}, Case{"asteroid", 12.0f, 100.0f}, Case{"asteroid", 60.0f, 100.0f}, Case{"planet", 400.0f, 100.0f}, Case{"moon", 50.0f, 150.0f}, Case{"station", 32.0f, 100.0f}, Case{"asteroid", 2.0f, 5.0f}})
        CHECK(std::fabs(ship::rules::contactDamage(c.kind, c.radius, c.speed, p) - ship::rules::collisionDamage(c.kind, c.radius, c.speed, p)) < 1e-6f);
    CHECK(std::fabs(ship::rules::contactDamage("asteroid", 2.0f, 100.0f, p) - 5.0f) < 1e-4f);   // 100/200 * 10 (small rock)
    CHECK(std::fabs(ship::rules::contactDamage("planet", 400.0f, 100.0f, p) - 25.0f) < 1e-4f);  // 100/200 * 50
    CHECK(std::fabs(ship::rules::contactDamage("asteroid", 2.0f, 3.5f, p) - 1.0f) < 1e-4f);     // just above the threshold: the 1 HP minimum
}
TEST(contact_cooldown_timing_and_capacity) {
    ContactCooldown cd;
    CHECK(cd.ready(4));
    cd.arm(4, 0.6f);
    CHECK(!cd.ready(4));
    CHECK(cd.ready(5));                                                                  // another body is not affected
    cd.tick(0.3f);
    CHECK(!cd.ready(4));
    cd.tick(0.29f);
    CHECK(!cd.ready(4));                                                                 // 0.59 s: still cooling
    cd.tick(0.02f);
    CHECK(cd.ready(4));                                                                  // 0.61 s: ready again
    cd.arm(4, 0.6f);
    cd.tick(0.5f);
    cd.arm(4, 0.6f);                                                                     // re-arming the same body restarts its timer
    cd.tick(0.5f);
    CHECK(!cd.ready(4));
    cd.arm(6, 0.0f);                                                                     // a zero cooldown arms nothing
    CHECK(cd.ready(6));
    ContactCooldown many;
    for (int i = 0; i < 20; i++) many.arm(100 + i, 0.6f + 0.01f * (float)i);            // more bodies than slots: no crash, the newest are kept
    CHECK(!many.ready(119));
    many.tick(10.0f);
    for (int i = 0; i < 20; i++) CHECK(many.ready(100 + i));
    many.tick(-1.0f);                                                                    // a negative step does not resurrect timers
    CHECK(many.ready(100));
}
TEST(contact_resting_contact_projects_the_velocity_and_impacts_still_bounce) {
    const engine::Vec3 n{0, 0, -1};                                                      // toward the surface
    // scrape: pressing into the surface at 2 m/s while sliding sideways: the closing part is removed, the sliding stays
    engine::Vec3 v = ship::rules::velocityAfterContact({1.5f, 0, -2.0f}, n, 2.0f, 0.35f, 3.0f);
    CHECK(nearv(v, {1.5f, 0, 0}));
    // real impact: the old bounce, (1 + 0.35) x the closing speed reflected
    v = ship::rules::velocityAfterContact({0, 0, -100.0f}, n, 100.0f, 0.35f, 3.0f);
    CHECK(nearv(v, {0, 0, 35.0f}));
    v = ship::rules::velocityAfterContact({2.0f, 0, -50.0f}, n, 50.0f, 0.0f, 3.0f);
    CHECK(nearv(v, {2.0f, 0, 0}));                                                       // bounce 0: a dead stop against the surface
    // separating or touching without closing: unchanged
    CHECK(nearv(ship::rules::velocityAfterContact({0, 0, 4.0f}, n, 0.0f, 0.35f, 3.0f), {0, 0, 4.0f}));
    CHECK(nearv(ship::rules::velocityAfterContact({0, 0, 4.0f}, n, -3.0f, 0.35f, 3.0f), {0, 0, 4.0f}));
    CHECK(nearv(ship::rules::velocityAfterContact({0, 0, 4.0f}, n, std::nanf(""), 0.35f, 3.0f), {0, 0, 4.0f}));
    // a body moving into a parked ship (closing 4 m/s relative, the ship's own velocity 0): pushed away at 1.35 x 4 (an impact above the threshold)
    v = ship::rules::velocityAfterContact({0, 0, 0}, n, 4.0f, 0.35f, 3.0f);
    CHECK(nearv(v, {0, 0, 5.4f}));
    v = ship::rules::velocityAfterContact({0, 0, 0}, n, 2.0f, 0.35f, 3.0f);              // ... and at 2 m/s it just slides along with it
    CHECK(nearv(v, {0, 0, 2.0f}));
}
TEST(contact_holding_thrust_against_a_surface_for_five_seconds_deals_zero_damage) {
    Rest after = holdThrustAgainstSphere(false, 5.0f);
    CHECK(after.events > 0);                                                             // the ship really keeps touching the surface...
    CHECK(std::fabs(after.hp - 100.0f) < 1e-4f && after.hits == 0);                      // ...and takes no damage at all
    CHECK(after.maxIntoSpeed < 3.0f);                                                    // every one of those touches was a scrape
    Rest before = holdThrustAgainstSphere(true, 5.0f);                                   // the old rules: the bug this test guards against
    CHECK(before.hits > 20);
    CHECK(before.hp < 80.0f);                                                            // a fast drain: 1 HP per touch event
}
TEST(contact_repeat_hits_from_the_same_body_are_limited_by_the_cooldown) {
    CollisionParams p;
    ContactCooldown cd;
    float hp = 100;
    auto hit = [&](float speed) {                                                        // a 20 m/s impact on asteroid 9: 20/200 * 10 = 1 HP... use a planet for a visible number
        float d = ship::rules::contactDamage("planet", 400.0f, speed, p);
        if (d > 0 && cd.ready(9)) { hp -= d; cd.arm(9, p.cooldown); }
    };
    hit(100.0f);                                                                         // 25 HP
    CHECK(std::fabs(hp - 75.0f) < 1e-3f);
    cd.tick(0.1f); hit(100.0f);                                                          // bounced straight back into it 0.1 s later: no second hit
    CHECK(std::fabs(hp - 75.0f) < 1e-3f);
    cd.tick(0.4f); hit(100.0f);                                                          // 0.5 s: still cooling
    CHECK(std::fabs(hp - 75.0f) < 1e-3f);
    cd.tick(0.2f); hit(100.0f);                                                          // 0.7 s: a new impact hurts again
    CHECK(std::fabs(hp - 50.0f) < 1e-3f);
}
