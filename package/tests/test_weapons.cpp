#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include "combat/weapons/weapons_data.h"
#include "combat/weapons/weapons_rules.h"
#include "tests/test.h"

namespace {
using combat::Vec3d;
bool close(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b)); }
double len(const Vec3d& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
combat::WeaponDef blaster() { return combat::defaultWeapons()[0]; }
combat::WeaponDef beam() { return combat::defaultWeapons()[1]; }
} // namespace

TEST(weapons_default_definitions) {
    auto w = combat::defaultWeapons();
    CHECK_EQ((int)w.size(), 2);
    CHECK(w[0].kind == combat::Kind::Projectile); CHECK(w[1].kind == combat::Kind::Beam);
    CHECK(close(w[0].speed, 600.0));                                   // clearly faster than the ship (5 km/s warp aside), not instant
    CHECK(w[0].lifetime > 1.0f && w[0].rateOfFire > 1.0f && w[0].recoil > 0.0f);
    CHECK(w[1].beamDps > 0.0f && w[1].range > 100.0f && w[1].heatPerSecond > 0.0f);
    // the heat model must actually limit fire: sustained fire overheats
    CHECK(w[0].heatPerShot * w[0].rateOfFire > w[0].cooldownRate);
    CHECK(w[1].heatPerSecond > w[1].cooldownRate * 0.0f);
}

TEST(weapons_bolt_inherits_shooter_velocity_and_moves_newtonian) {
    Vec3d ship{100, 0, -20}, dir{0, 0, -1};
    Vec3d v = combat::boltVelocity(ship, dir, 600.0);
    CHECK(close(v.x, 100.0)); CHECK(close(v.z, -620.0));               // ship velocity + 600 m/s along the aim
    Vec3d p = combat::boltStep({0, 0, 0}, v, 0.5);
    CHECK(close(p.x, 50.0)); CHECK(close(p.z, -310.0));
    // fired sideways from a moving ship: the sideways drift is kept
    Vec3d v2 = combat::boltVelocity({0, 0, -50}, {1, 0, 0}, 600.0);
    CHECK(close(v2.x, 600.0)); CHECK(close(v2.z, -50.0));
    // recoil pushes the ship back, scaled by the tunable; scale 0 disables
    Vec3d r = combat::recoilVelocity({0, 0, 0}, {0, 0, -1}, 0.5, 1.0);
    CHECK(close(r.z, 0.5));
    Vec3d r2 = combat::recoilVelocity({0, 0, 0}, {0, 0, -1}, 0.5, 0.0);
    CHECK(close(len(r2), 0.0));
    Vec3d r3 = combat::recoilVelocity({1, 2, 3}, {0, 0, -1}, 0.5, 2.0);
    CHECK(close(r3.z, 4.0)); CHECK(close(r3.x, 1.0));
}

TEST(weapons_segment_sphere_sweep) {
    double t;
    // straight hit
    CHECK(combat::segmentSphere({0, 0, 0}, {0, 0, -100}, {0, 0, -50}, 10.0, t)); CHECK(close(t, 0.4));   // touches at z = -40
    // a fast bolt that jumps clean over a small rock in one step is still caught (no tunnelling)
    CHECK(combat::segmentSphere({0, 0, 0}, {0, 0, -5000}, {0, 0, -2500}, 1.0, t)); CHECK(close(t, 2499.0 / 5000.0));
    // clean miss
    CHECK(!combat::segmentSphere({0, 0, 0}, {0, 0, -100}, {30, 0, -50}, 10.0, t));
    // tangent: grazing exactly touches (counts as a hit), just outside misses
    CHECK(combat::segmentSphere({0, 0, 0}, {0, 0, -100}, {10, 0, -50}, 10.0, t));
    CHECK(!combat::segmentSphere({0, 0, 0}, {0, 0, -100}, {10.01, 0, -50}, 10.0, t));
    // start inside: immediate hit at t = 0
    CHECK(combat::segmentSphere({0, 0, -50}, {0, 0, -100}, {0, 0, -55}, 10.0, t)); CHECK(close(t, 0.0));
    // the sphere is behind the start, or beyond the end: no hit this step
    CHECK(!combat::segmentSphere({0, 0, 0}, {0, 0, -100}, {0, 0, 50}, 10.0, t));
    CHECK(!combat::segmentSphere({0, 0, 0}, {0, 0, -100}, {0, 0, -200}, 10.0, t));
    // zero-length segment: only a hit if already inside
    CHECK(!combat::segmentSphere({0, 0, 0}, {0, 0, 0}, {0, 0, -50}, 10.0, t));
    CHECK(combat::segmentSphere({0, 0, 0}, {0, 0, 0}, {0, 0, -5}, 10.0, t));
    // far from the origin (double precision): 350,000 units out, a 3-unit rock, a 5 km/s bolt step of 83 units
    Vec3d a{350000.0, 100.0, -200000.0};
    CHECK(combat::segmentSphere(a, {a.x, a.y, a.z - 83.0}, {a.x + 1.0, a.y, a.z - 40.0}, 3.0, t));
}

TEST(weapons_ray_sphere_beam) {
    double d;
    CHECK(combat::raySphere({0, 0, 0}, {0, 0, -1}, 500.0, {0, 0, -100}, 10.0, d)); CHECK(close(d, 90.0));
    CHECK(!combat::raySphere({0, 0, 0}, {0, 0, -1}, 500.0, {0, 0, -600}, 10.0, d));    // beyond the range
    CHECK(!combat::raySphere({0, 0, 0}, {0, 0, -1}, 500.0, {0, 0, 100}, 10.0, d));     // behind
    CHECK(!combat::raySphere({0, 0, 0}, {0, 0, -1}, 500.0, {20, 0, -100}, 10.0, d));   // beside
    CHECK(combat::raySphere({0, 0, 0}, {0, 0, -1}, 500.0, {0, 0, -3}, 10.0, d)); CHECK(close(d, 0.0));   // origin inside
    // nearest of several
    struct S { Vec3d c; double r; } spheres[3] = {{{0, 0, -300}, 20}, {{0, 0, -100}, 10}, {{5, 0, -200}, 10}};
    double best = 500.0; int hit = -1;
    for (int i = 0; i < 3; i++) if (combat::raySphere({0, 0, 0}, {0, 0, -1}, best, spheres[i].c, spheres[i].r, d) && d < best) { best = d; hit = i; }
    CHECK_EQ(hit, 1); CHECK(close(best, 90.0));
}

TEST(weapons_heat_rate_of_fire_and_lockout_state_machine) {
    auto w = blaster();                                                  // 5 shots/s, 0.1 heat per shot, cools 0.25/s, lockout 2 s
    combat::HeatState s; bool tipped;
    CHECK(combat::canFire(s));
    CHECK(combat::fireShot(s, w, tipped)); CHECK(!tipped); CHECK(close(s.heat, 0.1));
    CHECK(!combat::canFire(s));                                          // rate of fire: must wait 0.2 s
    CHECK(!combat::fireShot(s, w, tipped)); CHECK(close(s.heat, 0.1));    // a refused shot costs nothing
    combat::tickHeat(s, w, 0.1f); CHECK(!combat::canFire(s));
    combat::tickHeat(s, w, 0.11f); CHECK(combat::canFire(s));
    // hold the trigger for a long time at 60 steps per second: it overheats, locks out, then recovers
    combat::HeatState h; int shots = 0; bool overheated = false; double tOver = 0, tBack = 0, t = 0;
    for (int i = 0; i < 60 * 30; i++) {
        t += 1.0 / 60;
        combat::tickHeat(h, w, 1.0f / 60);
        bool tip;
        if (combat::fireShot(h, w, tip)) shots++;
        if (tip && !overheated) { overheated = true; tOver = t; }
        if (overheated && tBack == 0 && !h.overheated) tBack = t;
        CHECK(h.heat >= 0.0f && h.heat <= 1.0f);
    }
    CHECK(overheated);
    CHECK(tOver > 3.0 && tOver < 6.0);                                   // net +0.25 heat/s from 0: about 4 s of fire
    CHECK(tBack - tOver >= w.lockoutSeconds - 1e-3);                     // cannot fire during the lockout
    CHECK(shots > 30 && shots < 5 * 30);                                 // far fewer shots than an unlimited 5 per second over 30 s (150)
    // locked out: even a ready timer does not allow a shot; after recovery the heat is below the recovery threshold
    combat::HeatState o; o.heat = 1.0f; o.overheated = true; o.lockout = 2.0f;
    CHECK(!combat::canFire(o));
    combat::tickHeat(o, w, 2.5f); CHECK(!o.overheated); CHECK(o.heat <= combat::kRecoverHeat + 1e-6);
    // lockout over but still too hot (slow cooling): stays locked
    auto slow = w; slow.cooldownRate = 0.01f;
    combat::HeatState q; q.heat = 1.0f; q.overheated = true; q.lockout = 1.0f;
    combat::tickHeat(q, slow, 2.0f); CHECK(q.overheated);
    combat::tickHeat(q, slow, 0.0f); combat::tickHeat(q, slow, -1.0f);   // dt <= 0: nothing changes
    CHECK(q.overheated);
}

TEST(weapons_beam_heat) {
    auto w = beam();                                                     // 0.15 heat per second, cools 0.3/s, lockout 2.5 s
    combat::HeatState s; bool tipped = false; double t = 0; bool tippedEver = false;
    for (int i = 0; i < 60 * 20 && !tippedEver; i++) { combat::tickHeat(s, w, 1.0f / 60); combat::fireBeam(s, w, 1.0f / 60, tipped); t += 1.0 / 60; tippedEver = tipped; }
    CHECK(!tippedEver);                                                  // 0.15 < 0.3 cooling: the default beam never overheats on its own
    auto hot = w; hot.heatPerSecond = 0.6f;
    combat::HeatState h; t = 0; tippedEver = false;
    for (int i = 0; i < 60 * 20 && !tippedEver; i++) { combat::tickHeat(h, hot, 1.0f / 60); combat::fireBeam(h, hot, 1.0f / 60, tipped); t += 1.0 / 60; tippedEver = tipped; }
    CHECK(tippedEver); CHECK(t > 2.5 && t < 4.5);                        // net 0.3/s: about 3.3 s
    bool tip2; CHECK(!combat::fireBeam(h, hot, 1.0f / 60, tip2));        // locked out
}

TEST(weapons_pool_capacity_and_slot_reuse) {
    combat::BoltPool pool; pool.init(4);
    for (int i = 0; i < 4; i++) CHECK(pool.spawn({(double)i, 0, 0}, {0, 0, -1}, 2.0f, 10.0f, 0));
    CHECK_EQ(pool.n, 4);
    CHECK(!pool.spawn({9, 0, 0}, {0, 0, -1}, 2.0f, 10.0f, 0));           // full: dropped, no crash
    CHECK_EQ(pool.dropped, 1); CHECK_EQ(pool.n, 4);
    pool.remove(1);                                                      // swap-remove keeps the live ones contiguous
    CHECK_EQ(pool.n, 3);
    CHECK(close(pool.px[1], 3.0));                                       // the last one took slot 1
    CHECK(pool.spawn({7, 0, 0}, {0, 0, -1}, 2.0f, 5.0f, 0)); CHECK_EQ(pool.n, 4);
    CHECK(close(pool.px[3], 7.0)); CHECK(close(pool.damage[3], 5.0));
    for (int i = 3; i >= 0; i--) pool.remove(i);
    CHECK_EQ(pool.n, 0);
    combat::BoltPool none; none.init(0); CHECK(!none.spawn({}, {}, 1, 1, 0));   // capacity 0 (combat.max_projectiles 0): nothing fires, no crash
}

TEST(weapons_aim_spread_and_muzzle) {
    combat::Rng rng(5);
    Vec3d axis{0, 0, -1};
    for (int i = 0; i < 300; i++) {
        Vec3d d = combat::spreadDirection(rng, axis, 2.0);
        CHECK(close(len(d), 1.0, 1e-9));
        CHECK(-d.z >= std::cos(2.0 * 3.14159265358979 / 180.0) - 1e-9);   // inside the cone
    }
    Vec3d z = combat::spreadDirection(rng, axis, 0.0); CHECK(close(z.z, -1.0));
    // muzzle: 3.6 units ahead, 0.3 below, in the ship frame
    float off[3] = {0.0f, -0.3f, 3.6f};
    Vec3d m = combat::muzzlePosition({10, 20, 30}, {0, 0, -1}, {0, 1, 0}, off);
    CHECK(close(m.x, 10.0)); CHECK(close(m.y, 19.7)); CHECK(close(m.z, 26.4));
    float side[3] = {1.0f, 0.0f, 0.0f};
    Vec3d r = combat::muzzlePosition({0, 0, 0}, {0, 0, -1}, {0, 1, 0}, side);       // right of a ship facing -Z is +X
    CHECK(close(r.x, 1.0));
}

TEST(weapons_data_parsing_defaults_and_sanitising) {
    std::string err;
    engine::Json j = engine::Json::parse(R"({"kind":"beam","name":"Cutter","beam_dps":55,"range":9999,"colour":[2,0.5,-1],"muzzle":[1,2,3],"rate_of_fire":500,"spread":99})", &err);
    combat::WeaponDef w = combat::weaponFromJson("cutter", j);
    CHECK(w.kind == combat::Kind::Beam); CHECK_EQ(w.name, std::string("Cutter"));
    CHECK(close(w.beamDps, 55.0)); CHECK(close(w.range, 9999.0));
    CHECK(close(w.colour[0], 1.0) && close(w.colour[2], 0.0));           // clamped
    CHECK(close(w.muzzle[2], 3.0));
    CHECK(w.rateOfFire <= 60.0f && w.spread <= 45.0f);                   // sanitised
    CHECK(close(w.speed, 600.0));                                        // a missing field keeps the default
    combat::WeaponDef e = combat::weaponFromJson("empty", engine::Json::object());
    CHECK_EQ(e.name, std::string("empty")); CHECK(e.kind == combat::Kind::Projectile); CHECK(close(e.damage, 10.0));
    engine::Json bad = engine::Json::parse(R"({"damage":-5,"lifetime":-1,"cooldown_rate":-2})", &err);
    combat::WeaponDef b = combat::weaponFromJson("bad", bad);
    CHECK(b.damage >= 0 && b.lifetime > 0 && b.cooldownRate >= 0);
}

TEST(weapons_shipped_data_file_is_valid_and_matches_defaults) {
    std::ifstream f("data/weapons.json");                                // tests run from the repo root
    CHECK(f.good());
    std::stringstream ss; ss << f.rdbuf();
    std::string err;
    engine::Json j = engine::Json::parse(ss.str(), &err);
    CHECK(j.isObject());
    auto defs = combat::defaultWeapons();
    combat::WeaponDef b = combat::weaponFromJson("blaster", j["blaster"], defs[0]), m = combat::weaponFromJson("mining_beam", j["mining_beam"], defs[1]);
    CHECK(close(b.damage, defs[0].damage)); CHECK(close(b.speed, defs[0].speed)); CHECK(close(b.rateOfFire, defs[0].rateOfFire));
    CHECK(close(b.heatPerShot, defs[0].heatPerShot)); CHECK(close(b.recoil, defs[0].recoil)); CHECK(b.kind == combat::Kind::Projectile);
    CHECK(close(m.beamDps, defs[1].beamDps)); CHECK(close(m.range, defs[1].range)); CHECK(m.kind == combat::Kind::Beam);
    CHECK(close(m.heatPerSecond, defs[1].heatPerSecond));
}

TEST(weapons_cpu_cost_of_100_live_bolts) {
    combat::BoltPool pool; pool.init(128);
    for (int i = 0; i < 100; i++) pool.spawn({(double)i, 0, 0}, {0, 0, -600}, 1000.0f, 10.0f, 0);
    struct S { Vec3d c; double r; };
    std::vector<S> rocks; for (int i = 0; i < 256; i++) rocks.push_back({{(i % 16) * 200.0 - 1600.0, (i / 16) * 200.0 - 1600.0, -1000.0}, 8.0});   // the nearby-asteroid cache
    auto t0 = std::chrono::steady_clock::now();
    long hits = 0;
    for (int step = 0; step < 300; step++) {
        for (int i = 0; i < pool.n; i++) {
            Vec3d p0{pool.px[i], pool.py[i], pool.pz[i]}, p1 = combat::boltStep(p0, {pool.vx[i], pool.vy[i], pool.vz[i]}, 1.0 / 60);
            double stepLen = len(combat::sub(p1, p0));
            for (auto& r : rocks) {
                if (len(combat::sub(r.c, p0)) > stepLen + r.r + 1.0) continue;     // the same cheap reject as the module
                double t; if (combat::segmentSphere(p0, p1, r.c, r.r, t)) hits++;
            }
            pool.px[i] = p1.x; pool.py[i] = p1.y; pool.pz[i] = p1.z;
        }
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 300.0;
    std::fprintf(stderr, "  [combat] 100 live bolts x 256 nearby asteroids: %.4f ms per step (%ld hits)\n", ms, hits);
    CHECK(ms < 2.0);
}
