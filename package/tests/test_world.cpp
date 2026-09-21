#include <cmath>
#include "tests/test.h"
#include "world/skybox/skybox_rules.h"
#include "world/starfield/starfield_rules.h"
#include "world/star_system/star_system_rules.h"

namespace {
bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
} // namespace

TEST(world_capped_size_keeps_aspect_and_never_enlarges) {
    auto a = world::cappedSize(8192, 4096, 1024);
    CHECK_EQ(a.w, 1024); CHECK_EQ(a.h, 512);
    auto b = world::cappedSize(4096, 8192, 1024);
    CHECK_EQ(b.w, 512); CHECK_EQ(b.h, 1024);
    auto c = world::cappedSize(512, 256, 1024);            // smaller than the cap: unchanged
    CHECK_EQ(c.w, 512); CHECK_EQ(c.h, 256);
    auto d = world::cappedSize(1024, 1024, 1024);
    CHECK_EQ(d.w, 1024); CHECK_EQ(d.h, 1024);
    auto e = world::cappedSize(8192, 4096, 0);             // <1 counts as 1
    CHECK_EQ(e.w, 1); CHECK_EQ(e.h, 1);
    auto f = world::cappedSize(8192, 4096, -5);
    CHECK_EQ(f.w, 1); CHECK_EQ(f.h, 1);
    auto g = world::cappedSize(10000, 3, 100);             // thin image: short side stays >= 1
    CHECK_EQ(g.w, 100); CHECK_EQ(g.h, 1);
}

TEST(world_face_name_mapping) {
    CHECK_EQ(world::matchFace("front.png"), 0);
    CHECK_EQ(world::matchFace("BACK.PNG"), 1);
    CHECK_EQ(world::matchFace("sky_lf.jpg"), 2);
    CHECK_EQ(world::matchFace("right.png"), 3);
    CHECK_EQ(world::matchFace("top.png"), 4);
    CHECK_EQ(world::matchFace("bot.png"), 5);
    CHECK_EQ(world::matchFace("sky_dn.png"), 5);
    CHECK_EQ(world::matchFace("down.png"), 5);
    CHECK_EQ(world::matchFace("readme.png"), -1);
    CHECK_EQ(std::string(world::faceName(5)), std::string("bot"));
    CHECK_EQ(std::string(world::faceJsonKey(5)), std::string("bottom"));
    for (int i = 0; i < 6; i++) CHECK_EQ(world::matchFace(std::string(world::faceName(i)) + ".png"), i);   // canonical names round-trip
}

TEST(world_face_uv_transform_all_combinations) {
    // corners (0,0) (1,0) (1,1) (0,1), computed by hand from the old skybox.h rules
    struct Case { bool fu, fv; int rot; float expect[4][2]; };
    const Case cases[] = {
        {false, false, 0,   {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
        {true,  false, 0,   {{1, 0}, {0, 0}, {0, 1}, {1, 1}}},
        {false, true,  0,   {{0, 1}, {1, 1}, {1, 0}, {0, 0}}},
        {true,  true,  0,   {{1, 1}, {0, 1}, {0, 0}, {1, 0}}},
        {false, false, 90,  {{0, 1}, {0, 0}, {1, 0}, {1, 1}}},
        {false, false, 180, {{1, 1}, {0, 1}, {0, 0}, {1, 0}}},
        {false, false, 270, {{1, 0}, {1, 1}, {0, 1}, {0, 0}}},
        {true,  true,  90,  {{1, 0}, {1, 1}, {0, 1}, {0, 0}}},
    };
    const float in[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (auto& c : cases) {
        world::FaceUV uv; uv.flipU = c.fu; uv.flipV = c.fv; uv.rotate = c.rot;
        for (int k = 0; k < 4; k++) {
            float u, v;
            uv.transform(in[k][0], in[k][1], u, v);
            CHECK(near(u, c.expect[k][0])); CHECK(near(v, c.expect[k][1]));
        }
    }
    CHECK_EQ(world::normalizeRotate(450), 90);
    CHECK_EQ(world::normalizeRotate(-90), 270);
    CHECK_EQ(world::normalizeRotate(45), 0);
}

TEST(world_choose_set) {
    std::vector<std::string> names = {"blue/set1", "dark/set1", "dark/set2"};
    CHECK_EQ(world::chooseSet(names, "dark/set2"), 2);
    CHECK_EQ(world::chooseSet(names, "dark/set1"), 1);
    CHECK_EQ(world::chooseSet(names, "nope/set9"), 0);      // fallback: first
    CHECK_EQ(world::chooseSet(names, ""), 0);
    CHECK_EQ(world::chooseSet({}, "dark/set1"), -1);        // nothing found
}

TEST(world_warp_streak_length) {
    CHECK(near(world::warpStreakLength(false, 2000, 2000, 300), 0));     // not warping
    CHECK(near(world::warpStreakLength(true, 0, 2000, 300), 0));
    CHECK(near(world::warpStreakLength(true, 1000, 2000, 300), 150));    // proportional
    CHECK(near(world::warpStreakLength(true, 2000, 2000, 300), 300));
    CHECK(near(world::warpStreakLength(true, 9000, 2000, 300), 300));    // capped
    CHECK(near(world::warpStreakLength(true, 2000, 2000, 0), 0));        // disabled by tunable
    CHECK(near(world::warpStreakLength(true, 2000, 0, 300), 0));         // bad ref speed
}

TEST(world_cache_path_and_freshness) {
    CHECK_EQ(world::cachePath("cache", "dark", "set1", 1024, "front"), std::string("cache/skybox/dark_set1_1024/front.png"));
    CHECK_EQ(world::cachePath("c", "red", "set3", 0, "bot"), std::string("c/skybox/red_set3_1/bot.png"));
    CHECK(world::cacheFresh(true, 200, 100));
    CHECK(world::cacheFresh(true, 100, 100));
    CHECK(!world::cacheFresh(true, 99, 100));                // source is newer
    CHECK(!world::cacheFresh(false, 500, 100));              // missing
}

TEST(world_box_downscale_averages_and_handles_pitch) {
    // 4x2 RGBA (bpp 4, padded pitch 20) -> 2x1: each output pixel averages a 2x2 block
    std::vector<uint8_t> src(20 * 2, 0);
    auto put = [&](int x, int y, int r, int g, int b) { uint8_t* p = &src[y * 20 + x * 4]; p[0] = r; p[1] = g; p[2] = b; p[3] = 255; };
    put(0, 0, 0, 0, 0);     put(1, 0, 100, 0, 0);   put(2, 0, 200, 200, 200); put(3, 0, 200, 200, 200);
    put(0, 1, 100, 0, 0);   put(1, 1, 0, 0, 0);     put(2, 1, 200, 200, 200); put(3, 1, 200, 200, 200);
    std::vector<uint8_t> out;
    world::boxDownscale(src.data(), 4, 2, 20, 4, 2, 1, out);
    CHECK_EQ(out.size(), (size_t)6);
    CHECK_EQ((int)out[0], 50); CHECK_EQ((int)out[1], 0); CHECK_EQ((int)out[2], 0);
    CHECK_EQ((int)out[3], 200); CHECK_EQ((int)out[4], 200); CHECK_EQ((int)out[5], 200);
    // no scaling: pixels copied, alpha dropped
    world::boxDownscale(src.data(), 4, 2, 20, 4, 4, 2, out);
    CHECK_EQ(out.size(), (size_t)24);
    CHECK_EQ((int)out[3], 100);
    // non-integer ratio never reads out of bounds (ASan checks) and stays in range
    std::vector<uint8_t> big(7 * 5 * 3, 128);
    world::boxDownscale(big.data(), 7, 5, 21, 3, 3, 2, out);
    for (auto v : out) CHECK_EQ((int)v, 128);
}

TEST(world_effective_face_uv_implicit_flip_on_top_and_bottom) {
    for (int face = 0; face < 6; face++) {
        bool vertical = face == 4 || face == 5;
        for (int fu = 0; fu < 2; fu++) for (int fv = 0; fv < 2; fv++) for (int rot : {0, 90, 180, 270}) {
            world::FaceUV j; j.flipU = fu; j.flipV = fv; j.rotate = rot;
            auto e = world::effectiveFaceUV(face, j);
            CHECK_EQ(e.flipV, vertical ? !(bool)fv : (bool)fv);   // top/bottom toggled, sides unchanged; json flip_v cancels it
            CHECK_EQ(e.flipU, (bool)fu);
            CHECK_EQ(e.rotate, rot);
        }
    }
    world::FaceUV none;
    CHECK(world::effectiveFaceUV(4, none).flipV);                 // no json: implicit flip alone
    world::FaceUV j; j.flipV = true;
    CHECK(!world::effectiveFaceUV(5, j).flipV);                   // json flip_v cancels it
}

// ---- star system ----
namespace {
double dist(const world::Vec3d& a, const world::Vec3d& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }
} // namespace

TEST(star_system_same_seed_same_system_different_seed_differs) {
    world::SystemParams p; p.seed = 1234;
    auto a = world::generateSystem(p), b = world::generateSystem(p);
    CHECK_EQ(a.bodies.size(), b.bodies.size());
    for (size_t i = 0; i < a.bodies.size(); i++) {
        CHECK(near(a.bodies[i].radius, b.bodies[i].radius));
        CHECK(std::fabs(a.bodies[i].orbitRadius - b.bodies[i].orbitRadius) < 1e-6);
        CHECK(std::fabs(a.bodies[i].period - b.bodies[i].period) < 1e-6);
        CHECK_EQ(a.bodies[i].name, b.bodies[i].name);
    }
    p.seed = 99;
    auto c = world::generateSystem(p);
    CHECK(std::fabs(c.bodies[1].orbitRadius - a.bodies[1].orbitRadius) > 1e-3 || c.bodies.size() != a.bodies.size());
}

TEST(star_system_structure_counts_and_ordering) {
    for (unsigned seed : {1u, 7u, 1234u, 99999u}) {
        world::SystemParams p; p.seed = seed; p.planets = 6;
        auto s = world::generateSystem(p);
        CHECK(s.bodies[0].kind == world::BodyKind::Sun);
        int planets = 0; double last = 0;
        for (size_t i = 1; i < s.bodies.size(); i++) {
            auto& b = s.bodies[i];
            CHECK_EQ(b.id, (int)i);
            CHECK(b.parent >= 0 && b.parent < (int)i);                // parent listed before its moons
            if (b.kind == world::BodyKind::Planet) { planets++; CHECK(b.orbitRadius > last); last = b.orbitRadius; CHECK(b.parent == 0); }
            else CHECK(s.bodies[b.parent].kind == world::BodyKind::Planet);
            CHECK(b.period > 0 && b.radius > 0);
        }
        CHECK_EQ(planets, 6);
        for (auto& b : s.bodies) if (b.kind == world::BodyKind::Planet) {
            int moons = 0; for (auto& m : s.bodies) if (m.parent == b.id) moons++;
            CHECK(moons >= 0 && moons <= 3);
        }
        CHECK(last > 60000.0);                                          // outer edge is tens of thousands of units out
    }
    world::SystemParams p; p.planets = 99; CHECK_EQ(world::generateSystem(p).bodies[0].kind == world::BodyKind::Sun, true);
    int cnt = 0; for (auto& b : world::generateSystem(p).bodies) cnt += b.kind == world::BodyKind::Planet;
    CHECK_EQ(cnt, 10);                                                  // clamped
    p.planets = 0; cnt = 0; for (auto& b : world::generateSystem(p).bodies) cnt += b.kind == world::BodyKind::Planet;
    CHECK_EQ(cnt, 2);
}

TEST(star_system_orbits_are_analytic_and_periodic) {
    world::SystemParams p;
    auto s = world::generateSystem(p);
    world::updatePositions(s, 0);
    auto at0 = s.bodies;
    for (auto& b : s.bodies) if (b.parent >= 0) CHECK(std::fabs(dist(b.position, s.bodies[b.parent].position) - b.orbitRadius) < 1e-6 * b.orbitRadius + 1e-6);
    // huge time: still exactly on the circle (no drift), and after whole periods back at the start
    world::updatePositions(s, 1.0e9);
    for (auto& b : s.bodies) if (b.parent >= 0) CHECK(std::fabs(dist(b.position, s.bodies[b.parent].position) - b.orbitRadius) < 1e-5 * b.orbitRadius);
    auto& pl = s.bodies[1];
    world::updatePositions(s, pl.period * 3.0);
    CHECK(dist(s.bodies[1].position, at0[1].position) < 1e-3 * pl.orbitRadius);
    // it actually moves
    world::updatePositions(s, pl.period * 0.25);
    CHECK(dist(s.bodies[1].position, at0[1].position) > 0.5 * pl.orbitRadius);
    // position depends on t only (same t, same answer, in any order)
    world::updatePositions(s, 5000); auto x1 = s.bodies[2].position;
    world::updatePositions(s, 9000); world::updatePositions(s, 5000);
    CHECK(dist(x1, s.bodies[2].position) < 1e-9);
}

TEST(star_system_spawn_is_clear_of_every_body) {
    for (unsigned seed : {1u, 42u, 1234u, 777u, 31337u}) {
        world::SystemParams p; p.seed = seed;
        auto s = world::generateSystem(p);
        for (double t : {0.0, 1e3, 1e4, 1e5, 1e6}) {
            world::updatePositions(s, t);
            for (auto& b : s.bodies) CHECK(dist(b.position, world::Vec3d{}) - b.radius > 5000.0);   // nothing within 5000 units of the origin
        }
    }
}

TEST(star_system_projection_keeps_angular_size_and_direction) {
    world::Vec3d cam{100, 0, 0}, body{100, 0, -60000};
    auto far = world::projectBody(body, 600.0, cam, 15000.0);
    CHECK(far.clamped);
    CHECK(near(far.z, -15000.0f)); CHECK(near(far.x, 0)); CHECK(near(far.dist, 60000.0f));
    CHECK(near(far.radius, 600.0f * 15000.0f / 60000.0f));
    CHECK(near(far.radius / std::sqrt(far.x * far.x + far.y * far.y + far.z * far.z), 600.0f / 60000.0f));   // same angular size
    auto close = world::projectBody(body, 600.0, {100, 0, -50000}, 15000.0);   // 10000 away: untouched
    CHECK(!close.clamped); CHECK(near(close.z, -10000.0f)); CHECK(near(close.radius, 600.0f));
    // precision: a body 1e6 units away and a camera next to it still gives an exact small offset (double subtraction)
    auto pre = world::projectBody({1.0e6 + 5.0, 0, 0}, 1.0, {1.0e6, 0, 0}, 15000.0);
    CHECK(near(pre.x, 5.0f)); CHECK(!pre.clamped);
    auto zero = world::projectBody({1, 2, 3}, 1.0, {1, 2, 3}, 15000.0);       // camera at the centre: no NaN
    CHECK(zero.x == zero.x && near(zero.radius, 1.0f));
}

TEST(star_system_sphere_mesh_is_valid) {
    auto m = world::buildSphere(16, 12);
    CHECK_EQ(m.verts.size(), (size_t)(13 * 17 * 3));
    CHECK_EQ(m.indices.size(), (size_t)(12 * 16 * 6));
    for (size_t i = 0; i < m.verts.size(); i += 3)
        CHECK(near(std::sqrt(m.verts[i] * m.verts[i] + m.verts[i + 1] * m.verts[i + 1] + m.verts[i + 2] * m.verts[i + 2]), 1.0f));
    unsigned short mx = 0; for (auto v : m.indices) mx = std::max(mx, v);
    CHECK(mx < m.verts.size() / 3);
}

// ---- star system collision wiring ----
TEST(star_system_physics_kinds_and_orbit_velocity) {
    CHECK_EQ(std::string(world::physicsKind(world::BodyKind::Sun)), std::string("sun"));
    CHECK_EQ(std::string(world::physicsKind(world::BodyKind::Planet)), std::string("planet"));
    CHECK_EQ(std::string(world::physicsKind(world::BodyKind::Moon)), std::string("moon"));
    // the sphere the physics world follows: a planet's velocity from two steps equals its tangential orbit speed
    world::SystemParams p;
    auto s = world::generateSystem(p);
    world::updatePositions(s, 1000.0);
    auto before = s.bodies[1].position;
    world::updatePositions(s, 1000.0 + 1.0 / 60);
    auto v = world::finiteVelocity(before, s.bodies[1].position, 1.0 / 60);
    double speed = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    double expect = 2.0 * 3.14159265358979 * s.bodies[1].orbitRadius / s.bodies[1].period;
    CHECK(std::fabs(speed - expect) < 1e-3 * expect);
    CHECK(speed > 5.0 && speed < 30.0);                                      // "10-15 units/s" order of magnitude
    auto z = world::finiteVelocity(before, before, 0.0);                     // dt 0: no NaN
    CHECK(z.x == 0.0 && z.y == 0.0 && z.z == 0.0);
    // a moon moves with its planet: its position over a step is planet motion plus its own orbit
    world::updatePositions(s, 500.0);
    for (auto& b : s.bodies) if (b.kind == world::BodyKind::Moon)
        CHECK(std::fabs(dist(b.position, s.bodies[b.parent].position) - b.orbitRadius) < 1e-6 * b.orbitRadius + 1e-6);
}
