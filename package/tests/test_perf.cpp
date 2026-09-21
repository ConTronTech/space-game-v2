// Tests for the performance pass: profiler, cockpit screen scheduler, screen canvas meshes, skybox face culling, preset row.
#include <cmath>
#include "core/quality/quality_rules.h"
#include "core/ui_handler/text_cache.h"
#include "engine/profiler.h"
#include "ship/cockpit/screen_canvas.h"
#include "ship/cockpit/screen_rate.h"
#include "tests/test.h"
#include "world/skybox/skybox_rules.h"

namespace {
bool nearp(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }
}

// ---- profiler ----
TEST(profiler_averages_worst_and_percent) {
    engine::Profiler p;
    int a = p.intern("mod:update"), b = p.intern("pass:sky");
    CHECK_EQ(p.intern("mod:update"), a);                                   // stable ids
    CHECK(a != b);
    for (unsigned long f = 0; f < engine::Profiler::kWarmupFrames + 10; f++) {
        p.beginFrame(f);
        p.add(a, 1.0);
        p.add(b, f % 2 ? 3.0 : 1.0);
        p.endFrame(10.0);
    }
    CHECK_EQ(p.frames(), 10ul);                                            // the warm-up frames are not counted
    auto rows = p.rows();
    CHECK_EQ(rows.size(), (size_t)2);
    CHECK_EQ(rows[0].name, std::string("pass:sky"));                       // sorted by average, largest first
    CHECK(nearp(rows[0].avgMs, 2.0) && nearp(rows[0].worstMs, 3.0));
    CHECK(nearp(rows[1].avgMs, 1.0) && nearp(rows[1].percent, 10.0));
    CHECK(nearp(p.avgFrameMs(), 10.0));
}
TEST(profiler_add_accumulates_within_a_frame_and_resets_between) {
    engine::Profiler p;
    int a = p.intern("fixed");
    for (unsigned long f = engine::Profiler::kWarmupFrames; f < engine::Profiler::kWarmupFrames + 4; f++) {
        p.beginFrame(f);
        p.add(a, 0.5); p.add(a, 0.5); p.add(a, 1.0);                       // several fixed steps in one frame
        p.endFrame(5.0);
    }
    CHECK(nearp(p.rows()[0].avgMs, 2.0));
    CHECK(nearp(p.rows()[0].worstMs, 2.0));
}
TEST(profiler_lists_slow_frames_with_the_three_worst_parts) {
    engine::Profiler p;
    int ids[5];
    const char* names[5] = {"a", "b", "c", "d", "e"};
    for (int i = 0; i < 5; i++) ids[i] = p.intern(names[i]);
    p.beginFrame(100);
    for (int i = 0; i < 5; i++) p.add(ids[i], (double)(i + 1));            // e=5, d=4, c=3, b=2, a=1
    p.endFrame(65.0);
    p.beginFrame(101);
    p.add(ids[0], 1.0);
    p.endFrame(16.0);                                                      // not slow
    CHECK_EQ(p.slowFrames().size(), (size_t)1);
    const auto& f = p.slowFrames()[0];
    CHECK_EQ(f.frame, 100ul);
    CHECK(nearp(f.ms, 65.0));
    CHECK_EQ(f.top.size(), (size_t)3);
    CHECK_EQ(f.top[0].first, std::string("e"));
    CHECK_EQ(f.top[2].first, std::string("c"));
    std::string rep = p.report();
    CHECK(rep.find("frame    100") != std::string::npos);
    CHECK(rep.find("e 5.00") != std::string::npos);
}
TEST(profiler_empty_and_bad_ids_do_not_crash) {
    engine::Profiler p;
    CHECK(p.report().find("PROFILE") != std::string::npos);
    p.beginFrame(0);
    p.add(-1, 1.0);
    p.add(99, 1.0);                                                        // unknown ids are ignored
    p.endFrame(1.0);
    CHECK(p.rows().empty());
    CHECK(nearp(p.avgFrameMs(), 0.0));
}
TEST(profiler_slow_list_is_bounded) {
    engine::Profiler p;
    int a = p.intern("x");
    for (unsigned long f = 0; f < engine::Profiler::kMaxSlowFrames + 50; f++) { p.beginFrame(f); p.add(a, 40.0); p.endFrame(40.0); }
    CHECK_EQ(p.slowFrames().size(), engine::Profiler::kMaxSlowFrames);
    CHECK(p.report().find("truncated") != std::string::npos);
}

// ---- cockpit screen scheduler ----
TEST(screen_rate_live_when_hz_is_zero) {
    cockpit::ScreenRate r;
    r.setHz(0);
    CHECK(r.live());
    for (double t = 0; t < 1; t += 0.001) CHECK(r.tick(t));
}
TEST(screen_rate_redraws_at_the_requested_rate) {
    cockpit::ScreenRate r;
    r.setHz(15);
    CHECK(!r.live());
    int redraws = 0;
    for (int i = 0; i < 600; i++) redraws += r.tick(i / 60.0) ? 1 : 0;     // 10 s of 60 fps frames
    CHECK(redraws >= 148 && redraws <= 152);                                // 15 Hz -> ~150
    cockpit::ScreenRate r30;
    r30.setHz(30);
    redraws = 0;
    for (int i = 0; i < 600; i++) redraws += r30.tick(i / 60.0) ? 1 : 0;
    CHECK(redraws >= 298 && redraws <= 302);
}
TEST(screen_rate_first_tick_draws_and_stalls_do_not_burst) {
    cockpit::ScreenRate r;
    r.setHz(10);
    CHECK(r.tick(5.0));                                                     // first call always draws
    CHECK(!r.tick(5.05));
    CHECK(r.tick(5.11));
    CHECK(r.tick(9.0));                                                     // a 4 s stall: one redraw, not forty
    CHECK(!r.tick(9.01));
    CHECK(!r.tick(9.05));
    CHECK(r.tick(9.11));
    r.invalidate();
    CHECK(r.tick(9.12));                                                    // forced
    CHECK(r.tick(1.0));                                                     // clock went backwards: redraw and carry on
    CHECK(!r.tick(1.01));
}

// ---- screen canvas (records quads, no GL) ----
namespace {
core::TaggedQuad unitQuad() {
    core::TaggedQuad q;                                                     // 2 x 1 metres facing +Z at z = -1
    q.corners[0] = {0, 0, -1}; q.corners[1] = {2, 0, -1}; q.corners[2] = {2, 1, -1}; q.corners[3] = {0, 1, -1};
    q.compute();
    return q;
}
}
TEST(canvas_records_quads_in_draw_order_with_brightness) {
    core::TaggedQuad q = unitQuad();
    cockpit::ScreenMesh m;
    cockpit::ScreenCanvas c(q, 0.5f, m);
    CHECK(nearp(c.aspect(), 2.0));
    c.fill({0.2f, 0.4f, 0.8f, 1});
    CHECK_EQ(m.vertexCount(), (size_t)4);
    CHECK(nearp(m.colors[0], 0.1) && nearp(m.colors[2], 0.4) && nearp(m.colors[3], 1.0));   // rgb scaled by brightness, alpha not
    c.rect(0, 0, 1, 0.5f, {1, 1, 1, 1});
    CHECK_EQ(m.vertexCount(), (size_t)8);
    CHECK_EQ(m.colors.size(), m.vertexCount() * 4);
    // the rect's top-left is the quad's top-left (corner 3), lifted a hair toward the viewer (normal is +Z here)
    CHECK(nearp(m.positions[12], 0.0, 1e-4) && nearp(m.positions[13], 1.0, 1e-4) && m.positions[14] > -1.0f && m.positions[14] < -0.99f);
    cockpit::ScreenMesh m2;
    cockpit::ScreenCanvas hot(q, 5.0f, m2);
    hot.fill({0.5f, 0.5f, 0.5f, 1});
    CHECK(nearp(m2.colors[0], 1.0));                                        // clamped
}
TEST(canvas_shapes_emit_whole_quads) {
    core::TaggedQuad q = unitQuad();
    cockpit::ScreenMesh m;
    cockpit::ScreenCanvas c(q, 1, m);
    c.line(0, 0, 1, 1, 0.01f, {1, 1, 1, 1});
    CHECK_EQ(m.vertexCount(), (size_t)4);
    c.frame(0.1f, 0.1f, 0.5f, 0.5f, 0.01f, {1, 1, 1, 1});
    CHECK_EQ(m.vertexCount(), (size_t)20);
    c.circle(1, 0.5f, 0.3f, 0.01f, {1, 1, 1, 1}, 24);
    CHECK_EQ(m.vertexCount(), (size_t)(20 + 24 * 4));
    size_t before = m.vertexCount();
    c.bar(0.1f, 0.1f, 0.5f, 0.1f, 0.0f, {1, 0, 0, 1});                      // empty bar: trough + frame only
    size_t emptyBar = m.vertexCount() - before;
    before = m.vertexCount();
    c.bar(0.1f, 0.1f, 0.5f, 0.1f, 0.5f, {1, 0, 0, 1});
    CHECK_EQ(m.vertexCount() - before, emptyBar + 4);
    before = m.vertexCount();
    c.text(0.1f, 0.1f, "", 0.1f, {1, 1, 1, 1});
    c.text(0.1f, 0.1f, "   ", 0.1f, {1, 1, 1, 1});                          // blanks draw nothing
    CHECK_EQ(m.vertexCount(), before);
    c.text(0.1f, 0.1f, "I", 0.1f, {1, 1, 1, 1});
    CHECK_EQ(m.vertexCount() - before, cockpit::glyphStrokes('I').size() * 4);
    for (float v : m.positions) CHECK(std::isfinite(v));
    m.clear();
    CHECK_EQ(m.vertexCount(), (size_t)0);
    CHECK(m.positions.capacity() > 0);                                      // rebuilding a screen reuses its memory
}
TEST(canvas_survives_a_degenerate_quad) {
    core::TaggedQuad q;                                                     // all corners equal: zero size
    q.compute();
    cockpit::ScreenMesh m;
    cockpit::ScreenCanvas c(q, 1, m);
    c.fill({1, 1, 1, 1});
    c.text(0.1f, 0.1f, "HI", 0.1f, {1, 1, 1, 1});
    c.circle(0.5f, 0.5f, 0.2f, 0.01f, {1, 1, 1, 1});
    for (float v : m.positions) CHECK(std::isfinite(v));
}

// ---- skybox face culling ----
namespace {
struct View { float m[16]; };
View viewLooking(float fx, float fy, float fz) {                            // camera looking along (fx,fy,fz), world up = +Y (or +Z when looking up/down)
    float len = std::sqrt(fx * fx + fy * fy + fz * fz);
    float f[3] = {fx / len, fy / len, fz / len};
    float up[3] = {0, 1, 0};
    if (std::fabs(f[1]) > 0.99f) { up[1] = 0; up[2] = -1; }
    float r[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
    float rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (float& v : r) v /= rl;
    float u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    View v{};
    for (int i = 0; i < 3; i++) { v.m[i * 4 + 0] = r[i]; v.m[i * 4 + 1] = u[i]; v.m[i * 4 + 2] = -f[i]; }   // column-major rows: right, up, -forward
    v.m[15] = 1;
    return v;
}
}
TEST(skybox_faces_default_view_shows_front_and_slivers_only) {
    View v = viewLooking(0, 0, -1);
    auto vis = world::visibleFaces(v.m, 60.0f, 16.0f / 9.0f);
    CHECK(vis.visible[0]);                                                  // front
    CHECK(!vis.visible[1]);                                                 // back is behind us
    CHECK(!vis.visible[4] && !vis.visible[5]);                              // top / bottom: outside a 60 degree vertical fov
    CHECK(vis.visible[2] && vis.visible[3]);                                // wide fov: the side faces' near edges are on screen
    CHECK_EQ(vis.count, 3);
    auto narrow = world::visibleFaces(v.m, 40.0f, 1.0f);
    CHECK_EQ(narrow.count, 1);                                              // a narrow view sees only the front face
}
TEST(skybox_faces_follow_the_view_direction) {
    View back = viewLooking(0, 0, 1);
    auto vb = world::visibleFaces(back.m, 60.0f, 1.5f);
    CHECK(vb.visible[1] && !vb.visible[0]);
    View right = viewLooking(1, 0, 0);
    auto vr = world::visibleFaces(right.m, 60.0f, 1.5f);
    CHECK(vr.visible[3] && !vr.visible[2]);
    View up = viewLooking(0, 1, 0);
    auto vu = world::visibleFaces(up.m, 60.0f, 1.5f);
    CHECK(vu.visible[4] && !vu.visible[5]);
    View down = viewLooking(0, -1, 0);
    auto vd = world::visibleFaces(down.m, 60.0f, 1.5f);
    CHECK(vd.visible[5] && !vd.visible[4]);
}
TEST(skybox_faces_never_hide_a_face_that_is_on_screen) {
    // brute force: sample directions on screen, the cube face each one hits must be marked visible
    const float fov = 70.0f, aspect = 16.0f / 9.0f;
    const float tanV = std::tan(fov * 0.5f * 3.14159265f / 180.0f), tanH = tanV * aspect;
    for (int yaw = 0; yaw < 360; yaw += 17)
        for (int pitch = -80; pitch <= 80; pitch += 20) {
            float ya = yaw * 3.14159265f / 180.0f, pa = pitch * 3.14159265f / 180.0f;
            float fx = std::sin(ya) * std::cos(pa), fy = std::sin(pa), fz = -std::cos(ya) * std::cos(pa);
            View v = viewLooking(fx, fy, fz);
            auto vis = world::visibleFaces(v.m, fov, aspect);
            for (float sx = -1; sx <= 1; sx += 0.5f)
                for (float sy = -1; sy <= 1; sy += 0.5f) {
                    // view-space ray -> world (transpose of the rotation)
                    float rx = sx * tanH, ry = sy * tanV, rz = -1;
                    float w[3];
                    for (int i = 0; i < 3; i++) w[i] = v.m[i * 4 + 0] * rx + v.m[i * 4 + 1] * ry + v.m[i * 4 + 2] * rz;
                    float ax = std::fabs(w[0]), ay = std::fabs(w[1]), az = std::fabs(w[2]);
                    int face = az >= ax && az >= ay ? (w[2] < 0 ? 0 : 1) : ax >= ay ? (w[0] < 0 ? 2 : 3) : (w[1] > 0 ? 4 : 5);
                    CHECK(vis.visible[face]);
                }
        }
}

// ---- quality preset row ----
TEST(quality_table_has_the_cockpit_screen_rate) {
    bool found = false;
    for (auto& e : quality::presetTable())
        if (std::string(e.key) == "cockpit.screen_hz") {
            found = true;
            CHECK_EQ(e.low, 15.0);
            CHECK_EQ(e.medium, 30.0);                                       // medium = the built-in default in cockpit.cpp
            CHECK(e.high >= 120.0 && e.ultra >= e.high);                    // at/above the frame rate = redrawn every frame: fast machines look exactly as before
        }
    CHECK(found);
}

// ---- UI text cache ----
TEST(text_cache_keys_by_size_and_string) {
    core::TextCache<int> c;
    CHECK(c.find(16, "SPEED", 1) == nullptr);
    c.insert(16, "SPEED", 7, 1);
    c.insert(22, "SPEED", 9, 1);                                           // same text, other font size: another entry
    c.insert(16, "speed", 5, 1);                                           // case matters
    CHECK_EQ(c.size(), (size_t)3);
    CHECK_EQ(*c.find(16, "SPEED", 2), 7);
    CHECK_EQ(*c.find(22, "SPEED", 2), 9);
    CHECK_EQ(*c.find(16, "speed", 2), 5);
    CHECK(c.find(18, "SPEED", 2) == nullptr);
    CHECK(c.find(16, "", 2) == nullptr);
    c.insert(16, "", 1, 2);                                                // the empty string is a valid key too
    CHECK_EQ(*c.find(16, "", 3), 1);
    c.insert(16, "SPEED", 8, 3);                                           // re-insert replaces, does not duplicate
    CHECK_EQ(c.size(), (size_t)4);
    CHECK_EQ(*c.find(16, "SPEED", 3), 8);
}
TEST(text_cache_pointers_survive_other_inserts) {
    core::TextCache<int> c;
    int* first = c.insert(16, "a", 1, 0);
    for (int i = 0; i < 2000; i++) c.insert(16 + i % 5, "s" + std::to_string(i), i, 0);   // forces rehashes
    CHECK(c.find(16, "a", 1) == first);                                    // node storage: the pointer is still good
}
TEST(text_cache_evicts_only_stale_entries) {
    core::TextCache<int> c;
    for (int i = 0; i < 10; i++) c.insert(16, "s" + std::to_string(i), i, 100);
    for (int i = 0; i < 4; i++) c.find(16, "s" + std::to_string(i), 150);   // 4 entries are still in use
    int released = 0, sum = 0;
    size_t gone = c.evictStale(200, 60, [&](int v) { released++; sum += v; });
    CHECK_EQ(gone, (size_t)6);                                              // used at 100, now 200: 100 > 60 frames old
    CHECK_EQ(released, 6);
    CHECK_EQ(sum, 4 + 5 + 6 + 7 + 8 + 9);
    CHECK_EQ(c.size(), (size_t)4);
    CHECK(c.find(16, "s0", 201) != nullptr && c.find(16, "s9", 201) == nullptr);
    released = 0;
    c.clear([&](int) { released++; });
    CHECK_EQ(released, 4);
    CHECK_EQ(c.size(), (size_t)0);
}
