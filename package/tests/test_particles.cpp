#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include "engine/json.h"
#include "fx/particles/particles_rules.h"
#include "tests/test.h"

namespace {
bool close(double a, double b, double tol = 1e-4) { return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b)); }
fx::Preset preset(const char* name) { auto v = fx::defaultPresets(); return *fx::findPreset(v, name); }
fx::Emit emitAt(double x, double y, double z) { fx::Emit e; e.position = {x, y, z}; return e; }
} // namespace

TEST(fx_default_presets_are_valid) {
    auto v = fx::defaultPresets();
    for (const char* n : {"exhaust", "spark", "debris", "warp_flash", "muzzle"}) CHECK(fx::findPreset(v, n) != nullptr);
    CHECK(fx::findPreset(v, "nope") == nullptr);
    for (auto& p : v) {
        fx::Preset q = p; fx::sanitize(q);
        CHECK_EQ(q.countMin, p.countMin); CHECK(close(q.speedMax, p.speedMax)); CHECK(close(q.lifeMin, p.lifeMin));   // already sane
        CHECK(p.countMin >= 0 && p.countMax >= p.countMin);
        CHECK(p.speedMax >= p.speedMin && p.lifeMax >= p.lifeMin && p.lifeMin > 0);
        CHECK(p.spreadDeg >= 0 && p.spreadDeg <= 180);
    }
    fx::Preset bad; bad.countMin = -5; bad.countMax = -9; bad.speedMin = 10; bad.speedMax = 2; bad.lifeMin = -1; bad.lifeMax = -2; bad.spreadDeg = 999; bad.colorStart[0] = 7;
    fx::sanitize(bad);
    CHECK(bad.countMin == 0 && bad.countMax == 0 && bad.speedMax >= bad.speedMin && bad.lifeMin > 0 && bad.lifeMax >= bad.lifeMin && bad.spreadDeg == 180.0f && bad.colorStart[0] == 1.0f);
}

TEST(fx_shipped_presets_file_matches_the_defaults) {
    std::ifstream f("data/particles.json");                              // tests run from the repo root
    CHECK(f.good());
    std::stringstream ss; ss << f.rdbuf();
    std::string err;
    engine::Json j = engine::Json::parse(ss.str(), &err);
    CHECK(j.isObject());
    for (auto& d : fx::defaultPresets()) {
        const engine::Json& e = j[d.name];
        CHECK(e.isObject());
        CHECK_EQ((int)e["count_min"].num(-1), d.countMin); CHECK_EQ((int)e["count_max"].num(-1), d.countMax);
        CHECK(close(e["speed_min"].num(-1), d.speedMin)); CHECK(close(e["speed_max"].num(-1), d.speedMax));
        CHECK(close(e["life_min"].num(-1), d.lifeMin)); CHECK(close(e["life_max"].num(-1), d.lifeMax));
        CHECK(close(e["size_start"].num(-1), d.sizeStart)); CHECK(close(e["size_end"].num(-1), d.sizeEnd));
        CHECK(close(e["drag"].num(-1), d.drag)); CHECK(close(e["spread"].num(-1), d.spreadDeg));
        CHECK_EQ(e["additive"].boolean(!d.additive), d.additive);
        for (int k = 0; k < 4; k++) { CHECK(close(e["color_start"].at(k).num(-1), d.colorStart[k])); CHECK(close(e["color_end"].at(k).num(-1), d.colorEnd[k])); }
    }
}

TEST(fx_pool_spawn_respects_capacity_and_budget_and_counts_drops) {
    fx::Pool pool; pool.init(20);
    fx::Rng rng(3);
    fx::Preset p = preset("spark");
    fx::Emit e = emitAt(0, 0, 0); e.count = 12;
    pool.newFrame(100);
    CHECK_EQ(fx::spawn(pool, p, e, rng), 12); CHECK_EQ(pool.n, 12);
    CHECK_EQ(fx::spawn(pool, p, e, rng), 8);                             // only 8 free slots
    CHECK_EQ(pool.n, 20); CHECK_EQ(pool.dropped, 4);
    CHECK_EQ(fx::spawn(pool, p, e, rng), 0);                             // full: nothing, and no crash
    CHECK_EQ(pool.dropped, 16);
    fx::Pool b; b.init(100); b.newFrame(5);                              // budget
    CHECK_EQ(fx::spawn(b, p, e, rng), 5); CHECK_EQ(fx::spawn(b, p, e, rng), 0); CHECK_EQ(b.n, 5); CHECK_EQ(b.dropped, 19);
    b.newFrame(100); CHECK_EQ(fx::spawn(b, p, e, rng), 12);              // a new frame refills the budget
    fx::Pool z; z.init(0); z.newFrame(10); CHECK_EQ(fx::spawn(z, p, e, rng), 0);   // zero capacity is harmless
    // no burst count given: a random count inside the preset's range
    fx::Pool c; c.init(100); c.newFrame(100); fx::Emit d = emitAt(0, 0, 0);
    int made = fx::spawn(c, p, d, rng);
    CHECK(made >= p.countMin && made <= p.countMax);
}

TEST(fx_spawn_is_deterministic_with_the_same_seed) {
    fx::Pool a, b; a.init(50); b.init(50); a.newFrame(50); b.newFrame(50);
    fx::Rng r1(42), r2(42);
    fx::Emit e = emitAt(1, 2, 3); e.count = 30; e.velocity = {5, 0, 0};
    fx::spawn(a, preset("debris"), e, r1); fx::spawn(b, preset("debris"), e, r2);
    for (int i = 0; i < a.n; i++) { CHECK(close(a.vx[i], b.vx[i], 1e-9)); CHECK(close(a.life[i], b.life[i], 1e-9)); CHECK(close(a.sizeA[i], b.sizeA[i], 1e-9)); }
    fx::Pool c; c.init(50); c.newFrame(50); fx::Rng r3(43); fx::spawn(c, preset("debris"), e, r3);
    bool differs = false; for (int i = 0; i < c.n; i++) if (std::fabs(c.vx[i] - a.vx[i]) > 1e-3) differs = true;
    CHECK(differs);
}

TEST(fx_spawn_speed_direction_and_inherited_velocity) {
    fx::Pool pool; pool.init(500); pool.newFrame(500);
    fx::Rng rng(9);
    fx::Preset p = preset("exhaust");                                    // 12 degree cone, speed 10-18
    fx::Emit e = emitAt(0, 0, 0); e.count = 200; e.direction = {0, 0, 1}; e.velocity = {100, 0, 0};
    fx::spawn(pool, p, e, rng);
    for (int i = 0; i < pool.n; i++) {
        double rx = pool.vx[i] - 100.0, ry = pool.vy[i], rz = pool.vz[i];      // velocity relative to the emitter
        double sp = std::sqrt(rx * rx + ry * ry + rz * rz);
        CHECK(sp >= p.speedMin - 1e-3 && sp <= p.speedMax + 1e-3);
        double ang = std::acos(std::clamp(rz / sp, -1.0, 1.0)) * 180.0 / 3.14159265358979;
        CHECK(ang <= p.spreadDeg + 1e-2);                                      // inside the cone around the emit direction
    }
    // (0,0,0) direction = everywhere: both hemispheres get particles
    fx::Pool q; q.init(500); q.newFrame(500); fx::Emit all = emitAt(0, 0, 0); all.count = 300;
    fx::spawn(q, preset("warp_flash"), all, rng);
    int up = 0, down = 0; for (int i = 0; i < q.n; i++) (q.vy[i] > 0 ? up : down)++;
    CHECK(up > 80 && down > 80);
}

TEST(fx_update_moves_ages_applies_drag_and_kills_without_holes) {
    fx::Pool pool; pool.init(10); pool.newFrame(10);
    fx::Preset p = preset("debris"); p.spreadDeg = 0; p.speedMin = p.speedMax = 10.0f; p.lifeMin = p.lifeMax = 1.0f; p.drag = 0.0f;
    fx::Rng rng(5);
    fx::Emit e = emitAt(0, 0, 0); e.count = 3; e.direction = {1, 0, 0};
    fx::spawn(pool, p, e, rng);
    fx::update(pool, 0.5f);
    for (int i = 0; i < pool.n; i++) { CHECK(close(pool.px[i], 5.0, 1e-4)); CHECK(close(pool.age[i], 0.5f)); }   // moved 10 m/s * 0.5 s
    // drag: velocity decays by exp(-drag dt)
    fx::Pool d; d.init(2); d.newFrame(2); fx::Preset q = p; q.drag = 2.0f;
    fx::Emit one = emitAt(0, 0, 0); one.count = 1; one.direction = {1, 0, 0};
    fx::spawn(d, q, one, rng); fx::update(d, 0.25f);
    CHECK(close(d.vx[0], 10.0 * std::exp(-0.5), 1e-4));
    // kill: after the life ends the pool is empty; staggered lives leave no holes (live particles stay the first n)
    fx::Pool k; k.init(50); k.newFrame(50);
    for (int i = 0; i < 20; i++) { fx::Preset s = p; s.lifeMin = s.lifeMax = 0.1f * (i + 1); fx::Emit one2 = emitAt(i, 0, 0); one2.count = 1; fx::spawn(k, s, one2, rng); }
    CHECK_EQ(k.n, 20);
    for (int step = 0; step < 10; step++) fx::update(k, 0.1f);                // 1.0 s: the ones living <= 1.0 s are gone (10 of them)
    CHECK(k.n >= 9 && k.n <= 11);
    for (int i = 0; i < k.n; i++) CHECK(k.age[i] < k.life[i] + 1e-4f);         // everything left is alive
    fx::update(k, 5.0f); CHECK_EQ(k.n, 0);
    fx::update(k, 0.0f); fx::update(k, -1.0f);                                 // dt <= 0: no change, no crash
}

TEST(fx_ramps_interpolate_colour_and_size) {
    fx::Pool pool; pool.init(4); pool.newFrame(4);
    fx::Preset p = preset("spark"); p.lifeMin = p.lifeMax = 2.0f; p.sizeStart = 1.0f; p.sizeEnd = 0.0f;
    for (int c = 0; c < 4; c++) { p.colorStart[c] = 1.0f; p.colorEnd[c] = 0.0f; }
    fx::Rng rng(1); fx::Emit e = emitAt(0, 0, 0); e.count = 1;
    fx::spawn(pool, p, e, rng);
    float col[4];
    fx::rampColour(pool, 0, col); CHECK(close(col[0], 1.0) && close(col[3], 1.0)); CHECK(close(fx::rampSize(pool, 0), 1.0));
    fx::update(pool, 1.0f);                                                     // half way
    fx::rampColour(pool, 0, col); CHECK(close(col[0], 0.5) && close(col[3], 0.5)); CHECK(close(fx::rampSize(pool, 0), 0.5));
    fx::update(pool, 0.9f);
    fx::rampColour(pool, 0, col); CHECK(col[0] >= 0.0f && col[0] < 0.1f);
    // size scale and override colour
    fx::Pool q; q.init(2); q.newFrame(2); fx::Emit s = emitAt(0, 0, 0); s.count = 1; s.sizeScale = 2.0f; s.sizeMul = 1.5f; s.colour[0] = 0.2f; s.colour[1] = 0.4f; s.colour[2] = 0.6f;
    fx::spawn(q, p, s, rng);
    CHECK(close(q.sizeA[0], 3.0)); CHECK(close(q.c0[0], 0.2) && close(q.c0[2], 0.6));
}

TEST(fx_quads_are_camera_relative_facing_culled_and_sorted_alpha_then_additive) {
    fx::Pool pool; pool.init(10); pool.newFrame(10);
    fx::Rng rng(2);
    fx::Preset add = preset("spark"), alpha = preset("debris");
    add.speedMin = add.speedMax = 0; alpha.speedMin = alpha.speedMax = 0;
    fx::Emit e = emitAt(1000.0, 0, -50.0); e.count = 1;
    fx::spawn(pool, add, e, rng);                                              // additive, spawned first
    fx::spawn(pool, alpha, e, rng);                                            // alpha-blended
    fx::Emit behind = emitAt(1000.0, 0, 50.0); behind.count = 1; fx::spawn(pool, alpha, behind, rng);   // behind the camera
    fx::Emit near = emitAt(1000.0, 0, -1.0); near.count = 1; fx::spawn(pool, alpha, near, rng);          // inside the 1.5 unit near cull
    fx::Emit far = emitAt(1000.0, 0, -9000.0); far.count = 1; fx::spawn(pool, alpha, far, rng);          // beyond max distance
    fx::Camera cam; cam.pos = {1000.0, 0, 0};                                  // looking down -Z at (1000, 0, 0)
    fx::QuadBuffers q; q.init(10);
    fx::QuadLimits lim;
    fx::buildQuads(pool, cam, lim, q);
    CHECK_EQ(q.alphaVerts, 4); CHECK_EQ(q.addVerts, 4);                        // 1 alpha + 1 additive survive the culling; alpha comes first
    // camera-relative: the quad is centred 50 units ahead, in float precision even though the world position is ~1000
    float cx = 0, cz = 0; for (int v = 0; v < 4; v++) { cx += q.pos[v * 3]; cz += q.pos[v * 3 + 2]; }
    CHECK(close(cx / 4, 0.0, 1e-4)); CHECK(close(cz / 4, -50.0, 1e-4));
    // the quad faces the camera: all four corners at the same depth, spanning right x up
    CHECK(close(q.pos[2], q.pos[5], 1e-5)); CHECK(q.pos[3] > q.pos[0]); CHECK(q.pos[10] > q.pos[1]);
    // the alpha quad is the debris (alpha ~1 at birth), the additive one is the spark
    CHECK(q.col[3] > 0.9f);
    // a tiny output buffer never overflows
    fx::QuadBuffers tiny; tiny.init(1); fx::buildQuads(pool, cam, lim, tiny); CHECK(tiny.alphaVerts + tiny.addVerts <= 4);
    // a wider near cull removes the closer ones too
    lim.nearCull = 60.0f; fx::buildQuads(pool, cam, lim, q); CHECK_EQ(q.alphaVerts + q.addVerts, 0);
}

TEST(fx_emit_rate_and_impact_counts) {
    float carry = 0; int total = 0;
    for (int i = 0; i < 60; i++) total += fx::emitCount(1.0f, 60.0f, 1.0f / 60, carry);       // one second at full thrust and 60 per second
    CHECK(total >= 59 && total <= 60);
    carry = 0; total = 0;
    for (int i = 0; i < 60; i++) total += fx::emitCount(0.5f, 60.0f, 1.0f / 60, carry);       // half thrust: half the particles
    CHECK(total >= 29 && total <= 30);
    carry = 0; CHECK_EQ(fx::emitCount(0.0f, 60.0f, 0.016f, carry), 0);                        // no thrust, no exhaust
    CHECK_EQ(fx::emitCount(1.0f, 0.0f, 0.016f, carry), 0);                                    // rate 0 = off
    carry = 0; CHECK_EQ(fx::emitCount(5.0f, 60.0f, 1.0f, carry), 60);                         // thrust level is capped at 1
    CHECK_EQ(fx::impactCount(0.0f, 40), 3); CHECK_EQ(fx::impactCount(50.0f, 40), 13); CHECK_EQ(fx::impactCount(5000.0f, 40), 40); CHECK_EQ(fx::impactCount(100.0f, 1), 3);   // scales with speed, clamped
}

TEST(fx_cpu_cost_of_2000_particles_is_small) {
    fx::Pool pool; pool.init(2000); pool.newFrame(2000);
    fx::Rng rng(7);
    fx::Emit e = emitAt(0, 0, -30); e.count = 2000;
    fx::Preset p = preset("spark"); p.lifeMin = p.lifeMax = 1000.0f;
    fx::spawn(pool, p, e, rng);
    fx::Camera cam; fx::QuadBuffers q; q.init(2000); fx::QuadLimits lim;
    auto t0 = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 200; frame++) { fx::update(pool, 1.0f / 60); fx::buildQuads(pool, cam, lim, q); }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 200.0;
    std::fprintf(stderr, "  [fx] 2000 live particles: update + build = %.3f ms per frame\n", ms);
    CHECK(ms < 5.0);                                                                            // a generous bound: the target is well under 0.5 ms on the dev machine
}

TEST(fx_random_direction_is_a_unit_vector_in_the_cone) {
    fx::Rng rng(11);
    for (int i = 0; i < 500; i++) {
        auto d = fx::randomDirection(rng, {0, 0, 0}, 180.0f);
        CHECK(close(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), 1.0, 1e-9));
        auto c = fx::randomDirection(rng, {3, 4, 0}, 30.0f);
        CHECK(close(std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z), 1.0, 1e-9));
        CHECK(c.x * 0.6 + c.y * 0.8 >= std::cos(30.0 * 3.14159265358979 / 180.0) - 1e-9);      // within 30 degrees of the axis (3,4,0)/5
        auto z = fx::randomDirection(rng, {0, 1, 0}, 0.0f);                                     // zero spread: exactly the axis
        CHECK(close(z.y, 1.0, 1e-9));
    }
}

TEST(fx_soft_dot_texture_is_round_and_soft) {
    std::vector<uint8_t> px;
    fx::softDotTexture(32, px);
    CHECK_EQ(px.size(), (size_t)(32 * 32 * 4));
    auto a = [&](int x, int y) { return (int)px[((size_t)y * 32 + x) * 4 + 3]; };
    CHECK(a(16, 16) > 240);                        // opaque in the middle
    CHECK(a(0, 0) == 0 && a(31, 0) == 0 && a(0, 31) == 0);   // corners transparent: it is a disc, not a square
    CHECK(a(16, 0) < 40 && a(0, 16) < 40);         // the rim fades out
    CHECK(a(16, 8) > a(16, 3) && a(16, 16) > a(16, 8));     // monotonic falloff from the centre
    CHECK_EQ((int)px[0], 255);                     // white colour, alpha does the shaping
    fx::softDotTexture(0, px); CHECK(px.size() >= 16);       // absurd size is clamped
}
