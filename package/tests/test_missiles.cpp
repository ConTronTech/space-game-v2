#include <chrono>
#include <cmath>
#include <cstdio>
#include "combat/weapons/missile_rules.h"
#include "combat/weapons/weapons_data.h"
#include "engine/json.h"
#include "tests/test.h"

namespace {
using combat::Vec3d;
bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
double dist(const Vec3d& a, const Vec3d& b) { return combat::length(combat::sub(a, b)); }

// Flies a missile until it gets within `hitR` of the (possibly moving) target or its lifetime ends. Returns the closest approach.
double fly(combat::MissileState& m, const combat::MissileParams& p, Vec3d tPos, const Vec3d& tVel, bool guided, double hitR, double& tHit) {
    const double dt = 1.0 / 60.0;
    double best = 1e18;
    tHit = -1;
    for (int k = 0; k < 60 * 30 && m.life > 0; k++) {
        combat::stepMissile(m, p, guided, tPos, tVel, dt);
        tPos = combat::add(tPos, combat::mul(tVel, dt));
        double d = dist(m.pos, tPos);
        best = std::min(best, d);
        if (d <= hitR) { tHit = k * dt; break; }
    }
    return best;
}
} // namespace

TEST(missile_ammo_rack_cap) {
    CHECK_EQ(combat::acceptMissiles(0, 12, 3), 3);
    CHECK_EQ(combat::acceptMissiles(10, 12, 3), 2);                     // partial: only what fits
    CHECK_EQ(combat::acceptMissiles(12, 12, 3), 0);                     // full: refused (the pack is not consumed)
    CHECK_EQ(combat::acceptMissiles(14, 12, 3), 0);
    int have = 1;
    CHECK(combat::takeMissile(have)); CHECK_EQ(have, 0);
    CHECK(!combat::takeMissile(have)); CHECK_EQ(have, 0);               // empty rack: no shot, nothing changes
}

TEST(missile_lock_ranking_cone_then_distance) {
    combat::LockParams p;
    std::vector<combat::LockCandidate> c = {
        {10, {0, 0, -1000}, 9},                                      // dead ahead, far
        {11, {0, 0, -300}, 9},                                       // dead ahead, near: best
        {12, {100, 0, -1000}, 9},                                    // ~5.7 deg off
        {13, {600, 0, -1000}, 9},                                    // ~31 deg: outside the 12 deg cone
        {14, {0, 0, -3000}, 9},                                      // beyond 2500
        {15, {0, 0, 500}, 9},                                        // behind
    };
    std::vector<int> r;
    combat::rankCandidates({0, 0, 0}, {0, 0, -1}, c, p, r);
    CHECK_EQ((int)r.size(), 3);
    CHECK_EQ(c[r[0]].id, 11); CHECK_EQ(c[r[1]].id, 10); CHECK_EQ(c[r[2]].id, 12);
}

TEST(missile_lock_acquire_cycle_clear) {
    combat::LockParams p;
    std::vector<combat::LockCandidate> c = {{1, {0, 0, -300}, 9}, {2, {50, 0, -1000}, 9}};
    std::vector<int> r;
    combat::rankCandidates({0, 0, 0}, {0, 0, -1}, c, p, r);
    combat::Lock l;
    CHECK_EQ(combat::pressLock(l, c, r), 1);
    CHECK(l.state == combat::LockState::Acquiring);
    // acquisition: 1 s inside the cone
    for (int k = 0; k < 59; k++) combat::updateLock(l, p, 1.0f / 60, {0, 0, 0}, {0, 0, -1}, true, c[0].pos);
    CHECK(l.state == combat::LockState::Acquiring);
    for (int k = 0; k < 2; k++) combat::updateLock(l, p, 1.0f / 60, {0, 0, 0}, {0, 0, -1}, true, c[0].pos);
    CHECK(l.state == combat::LockState::Locked);
    // T again: cycles to the next candidate (acquiring again)
    CHECK_EQ(combat::pressLock(l, c, r), 2);
    CHECK(l.state == combat::LockState::Acquiring && l.target == 2);
    CHECK_EQ(combat::pressLock(l, c, r), 1);                          // and wraps around
    // only one candidate and it is the current target: the second press clears
    std::vector<combat::LockCandidate> one = {{7, {0, 0, -500}, 9}};
    combat::rankCandidates({0, 0, 0}, {0, 0, -1}, one, p, r);
    combat::Lock l2;
    CHECK_EQ(combat::pressLock(l2, one, r), 7);
    CHECK_EQ(combat::pressLock(l2, one, r), -1);
    CHECK(l2.state == combat::LockState::Idle && l2.target == -1);
    // nothing in the cone: nothing happens
    r.clear(); combat::Lock l3;
    CHECK_EQ(combat::pressLock(l3, one, r), -1); CHECK(l3.state == combat::LockState::Idle);
}

TEST(missile_lock_loss_conditions) {
    combat::LockParams p;
    auto locked = [&]() { combat::Lock l; l.state = combat::LockState::Locked; l.target = 1; return l; };
    combat::Lock l = locked();
    combat::updateLock(l, p, 0.1f, {0, 0, 0}, {0, 0, -1}, false, {0, 0, -300});
    CHECK(l.state == combat::LockState::Lost); CHECK_EQ(std::string(l.why), std::string("target destroyed"));
    l = locked();
    combat::updateLock(l, p, 0.1f, {0, 0, 0}, {0, 0, -1}, true, {0, 0, -2600});
    CHECK(l.state == combat::LockState::Lost); CHECK_EQ(std::string(l.why), std::string("out of range"));
    // a locked target 20 deg off is kept (wider keep cone), 40 deg is lost
    l = locked();
    combat::updateLock(l, p, 0.1f, {0, 0, 0}, {0, 0, -1}, true, {std::tan(20 * combat::kPi / 180) * 500, 0, -500});
    CHECK(l.state == combat::LockState::Locked);
    combat::updateLock(l, p, 0.1f, {0, 0, 0}, {0, 0, -1}, true, {std::tan(40 * combat::kPi / 180) * 500, 0, -500});
    CHECK(l.state == combat::LockState::Lost); CHECK_EQ(std::string(l.why), std::string("left the cone"));
    // while ACQUIRING the narrow cone applies: 20 deg off loses it
    combat::Lock a; a.state = combat::LockState::Acquiring; a.target = 1;
    combat::updateLock(a, p, 0.1f, {0, 0, 0}, {0, 0, -1}, true, {std::tan(20 * combat::kPi / 180) * 500, 0, -500});
    CHECK(a.state == combat::LockState::Lost);
    // lost goes back to idle after lostShow
    for (int k = 0; k < 11; k++) combat::updateLock(a, p, 0.1f, {0, 0, 0}, {0, 0, -1}, true, {0, 0, -1});
    CHECK(a.state == combat::LockState::Idle && a.target == -1);
}

TEST(missile_autolock_nearest_in_cone) {
    std::vector<combat::LockCandidate> c = {
        {10, {0, 0, 200}, 5},                                        // nearer, but BEHIND the ship: excluded
        {11, {0, 0, -3000}, 5},                                      // ahead, out of range: excluded
        {12, {std::tan(60 * combat::kPi / 180) * 400, 0, -400}, 5},  // 60 deg off, 800 units: inside the 80 deg cone
        {13, {0, 0, -900}, 5},                                       // dead ahead, 900 units
        {14, {std::tan(85 * combat::kPi / 180) * 50, 0, -50}, 5},    // 85 deg off: outside the cone
    };
    int k = combat::nearestInCone({0, 0, 0}, {0, 0, -1}, c, 2500, 80);
    CHECK(k >= 0); CHECK_EQ(c[k].id, 12);
    CHECK(near(dist(c[k].pos, {0, 0, 0}), 800, 1e-6));
    // ties go to the lower id
    std::vector<combat::LockCandidate> t = {{7, {0, 0, -500}, 5}, {3, {500, 0, -0.0000001}, 5}, {5, {0, 500, 0}, 5}};
    k = combat::nearestInCone({0, 0, 0}, {0, 0, -1}, t, 2500, 90);
    CHECK_EQ(t[k].id, 3);
    // nothing ahead
    std::vector<combat::LockCandidate> b = {{1, {0, 0, 100}, 5}};
    CHECK_EQ(combat::nearestInCone({0, 0, 0}, {0, 0, -1}, b, 2500, 80), -1);
}

TEST(missile_autolock_hysteresis_pause_rate_disabled) {
    combat::AutoLockParams p;                                        // 4 Hz, 3 s pause
    combat::AutoLock a;
    const float dt = 1.0f / 60.0f;
    // rate limiter: 4 scans in one second of idle
    int scans = 0;
    for (int i = 0; i < 60; i++) scans += combat::autoLockDue(a, p, dt, true, combat::LockState::Idle);
    CHECK(scans >= 4 && scans <= 5);
    // not with another weapon, and never while a lock is held (no hopping)
    a = {}; CHECK(!combat::autoLockDue(a, p, dt, false, combat::LockState::Idle));
    a = {}; CHECK(!combat::autoLockDue(a, p, dt, true, combat::LockState::Locked));
    a = {}; CHECK(!combat::autoLockDue(a, p, dt, true, combat::LockState::Acquiring));
    a = {}; CHECK(combat::autoLockDue(a, p, dt, true, combat::LockState::Lost));
    // disabled = the old manual behaviour
    combat::AutoLockParams off; off.enabled = false; a = {};
    for (int i = 0; i < 120; i++) CHECK(!combat::autoLockDue(a, off, dt, true, combat::LockState::Idle));
    // manual clear pauses for 3 s
    a = {}; combat::autoLockManualClear(a, p);
    int first = -1;
    for (int i = 0; i < 300 && first < 0; i++) if (combat::autoLockDue(a, p, dt, true, combat::LockState::Idle)) first = i;
    CHECK(near(first * dt, 3.0, 2 * dt));
    // hysteresis in the state machine: an auto lock is kept at 70 deg (inside the 80 deg cone), a nearer rock does not matter; 85 deg loses it
    combat::LockParams lp;
    combat::Lock l; combat::startAutoLock(l, 12, false);
    CHECK(l.state == combat::LockState::Acquiring && l.autoPicked);
    Vec3d at70{std::tan(70 * combat::kPi / 180) * 300, 0, -300};
    for (int i = 0; i < 70; i++) combat::updateLock(l, lp, dt, {0, 0, 0}, {0, 0, -1}, true, at70);
    CHECK(l.state == combat::LockState::Locked && l.target == 12);
    combat::updateLock(l, lp, dt, {0, 0, 0}, {0, 0, -1}, true, {std::tan(85 * combat::kPi / 180) * 300, 0, -300});
    CHECK(l.state == combat::LockState::Lost);
    combat::Lock i2; combat::startAutoLock(i2, 4, true);
    CHECK(i2.state == combat::LockState::Locked);
}

TEST(missile_pn_hits_stationary_target_from_offset) {
    combat::MissileParams p;
    // launched straight ahead from a ship drifting sideways at 30 m/s; the rock is 800 ahead and 150 to the side
    combat::MissileState m = combat::launchMissile({0, 0, 0}, {30, 0, 0}, {0, 0, -1}, p);
    Vec3d target{-150, 40, -800};
    double t;
    double best = fly(m, p, target, {0, 0, 0}, true, 9.0, t);
    CHECK(best <= 9.0); CHECK(t > 0 && t < 10.0);
    // the same shot unguided flies straight and misses
    combat::MissileState u = combat::launchMissile({0, 0, 0}, {30, 0, 0}, {0, 0, -1}, p);
    CHECK(fly(u, p, target, {0, 0, 0}, false, 9.0, t) > 50.0);
    CHECK(std::fabs(u.heading.x) < 1e-9 && std::fabs(u.heading.z + 1.0) < 1e-9);   // the nose never turned
}

TEST(missile_pn_hits_constant_velocity_target) {
    combat::MissileParams p;
    combat::MissileState m = combat::launchMissile({0, 0, 0}, {0, 0, 0}, {0, 0, -1}, p);
    Vec3d target{0, 0, -1200}, tVel{60, 20, 0};                        // crossing at ~63 m/s
    double t;
    double best = fly(m, p, target, tVel, true, 9.0, t);
    CHECK(best <= 9.0);
    // PN leads: pure pursuit of the current position would trail; here the heading at impact points ahead of the target's start line
    CHECK(m.pos.x > 5.0);
}

TEST(missile_turn_rate_is_clamped) {
    combat::MissileParams p;
    combat::MissileState m = combat::launchMissile({0, 0, 0}, {0, 0, 0}, {0, 0, -1}, p);
    // the target is straight to the side: after 0.5 s the nose may have turned at most 45 deg
    for (int k = 0; k < 30; k++) combat::stepMissile(m, p, true, {1000, 0, 0}, {0, 0, 0}, 1.0 / 60);
    double a = combat::angleDeg(m.heading, {0, 0, -1});
    CHECK(a <= 45.0 + 0.1); CHECK(a >= 44.0);
    Vec3d h = combat::turnToward({0, 0, -1}, {1, 0, 0}, 10 * combat::kPi / 180);
    CHECK(near(combat::angleDeg(h, {0, 0, -1}), 10.0, 1e-6)); CHECK(near(combat::length(h), 1.0, 1e-9));
    Vec3d o = combat::turnToward({0, 0, -1}, {0, 0, 1}, 0.1);         // exactly opposite: still a clean unit turn
    CHECK(near(combat::length(o), 1.0, 1e-9)); CHECK(near(combat::angleDeg(o, {0, 0, -1}), 0.1 * 180 / combat::kPi, 1e-6));
}

TEST(missile_fuel_out_coasts_then_expires) {
    combat::MissileParams p;
    combat::MissileState m = combat::launchMissile({0, 0, 0}, {0, 0, 0}, {0, 0, -1}, p);
    double dt = 1.0 / 60;
    int steps = 0;
    while (m.fuel > 0) { combat::stepMissile(m, p, false, {}, {}, dt); steps++; }
    CHECK(near(steps * dt, p.fuel, 2 * dt));
    double vBurnout = combat::length(m.vel);
    CHECK(near(vBurnout, p.launchKick + p.thrust * p.fuel, 1.5));    // 40 + 60*8 = 520 m/s
    Vec3d h = m.heading;
    for (int k = 0; k < 60; k++) combat::stepMissile(m, p, true, {1000, 0, 0}, {}, dt);   // no fuel: no control, no thrust
    CHECK(near(combat::length(m.vel), vBurnout, 1e-9));
    CHECK(near(combat::angleDeg(m.heading, h), 0.0, 1e-9));
    while (m.life > 0) combat::stepMissile(m, p, false, {}, {}, dt);
    CHECK(m.life <= 0);                                                // the module explodes it now
}

TEST(missile_arming_and_swept_collision) {
    combat::MissileParams p;
    combat::MissileState m = combat::launchMissile({0, 0, 0}, {0, 0, 0}, {0, 0, -1}, p);
    CHECK(!combat::isArmed(m, p));
    int k = 0;
    while (!combat::isArmed(m, p) && k < 600) { combat::stepMissile(m, p, false, {}, {}, 1.0 / 60); k++; }
    CHECK(combat::isArmed(m, p)); CHECK(m.travelled >= 30.0 && m.travelled < 40.0);
    // at burnout speed (520 m/s) one step is ~8.7 units: a radius-3 rock between two samples is still hit by the swept test
    Vec3d p0{0, 0, 0}, p1{0, 0, -8.7};
    double t;
    CHECK(combat::segmentSphere(p0, p1, {0.5, 0, -4.3}, 3.0, t)); CHECK(t > 0 && t < 1);
}

TEST(missile_blast_damage_falloff) {
    combat::MissileParams p;
    CHECK(near(combat::blastDamage(0, p), 400, 1e-4));
    CHECK(near(combat::blastDamage(30, p), 200, 1e-3));
    CHECK(near(combat::blastDamage(60, p), 0, 1e-6));
    CHECK(near(combat::blastDamage(100, p), 0, 1e-6));
    CHECK(combat::blastDamage(0, p) >= 100.0f);                         // a radius-9 rock (~100 HP) dies to one direct hit
}

TEST(missile_pool_spawn_remove_no_growth) {
    combat::MissilePool pool;
    pool.init(2);
    combat::MissileParams p;
    auto a = combat::launchMissile({1, 0, 0}, {}, {0, 0, -1}, p), b = combat::launchMissile({2, 0, 0}, {}, {0, 0, -1}, p);
    CHECK(pool.spawn(a, 5, 0)); CHECK(pool.spawn(b, -1, 0)); CHECK(!pool.spawn(a, 5, 0));
    CHECK_EQ(pool.dropped, 1L);
    pool.remove(0);
    CHECK_EQ(pool.n, 1); CHECK(near(pool.px[0], 2.0, 1e-12)); CHECK_EQ(pool.target[0], -1);
    CHECK_EQ((int)pool.px.capacity() >= 2, true);
}

TEST(missile_weapon_from_json_and_save_round_trip) {
    engine::Json j = engine::Json::parse(R"({"name":"Missile","kind":"missile","rate_of_fire":0.6667,"thrust":70,"turn_rate":80,"fuel_seconds":6,"blast_radius":50,"damage":300})");
    auto w = combat::weaponFromJson("missile", j);
    CHECK(w.kind == combat::Kind::Missile);
    CHECK(near(w.missile.thrust, 70, 1e-9)); CHECK(near(w.missile.turnRateDeg, 80, 1e-9)); CHECK(near(w.missile.fuel, 6, 1e-6));
    CHECK(near(w.missile.blastRadius, 50, 1e-9)); CHECK(near(w.missile.maxDamage, 300, 1e-4));
    CHECK(near(1.0 / w.rateOfFire, 1.5, 0.01));
    CHECK(w.heatPerShot == 0.0f);                                       // not heat based
    // the save format of combat/weapons
    engine::Json s = combat::weaponsSave(7, 2);
    int missiles = 0, sel = 0;
    combat::weaponsLoad(engine::Json::parse(s.dump()), 12, 3, missiles, sel);
    CHECK_EQ(missiles, 7); CHECK_EQ(sel, 2);
    combat::weaponsLoad(engine::Json::parse(R"({"missiles":99,"selected":9})"), 12, 3, missiles, sel);
    CHECK_EQ(missiles, 12); CHECK_EQ(sel, 2);                          // clamped to the rack and the weapon list
    missiles = 4; sel = 1;
    combat::weaponsLoad(engine::Json::parse("{}"), 12, 3, missiles, sel);   // missing keys keep the current value
    CHECK_EQ(missiles, 4); CHECK_EQ(sel, 1);
}

TEST(missile_cpu_eight_missiles) {
    combat::MissileParams p;
    combat::MissilePool pool; pool.init(8);
    for (int i = 0; i < 8; i++) pool.spawn(combat::launchMissile({i * 10.0, 0, 0}, {}, {0, 0, -1}, p), 1, 0);
    auto t0 = std::chrono::steady_clock::now();
    const int steps = 600;
    for (int s = 0; s < steps; s++)
        for (int i = 0; i < pool.n; i++) {
            auto m = pool.get(i);
            combat::stepMissile(m, p, true, {0, 0, -5000}, {10, 0, 0}, 1.0 / 60);
            pool.set(i, m);
        }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / steps;
    std::fprintf(stderr, "  [perf] 8 missiles guidance step: %.5f ms\n", ms);
    CHECK(ms < 0.5);
}
