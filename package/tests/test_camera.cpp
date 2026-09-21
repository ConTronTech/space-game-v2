#include <cmath>
#include "core/camera/camera_math.h"
#include "tests/test.h"

using engine::Vec3;

namespace {
// transform a point by a column-major 4x4
Vec3 apply(const float m[16], const Vec3& p) {
    return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13], m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
}
bool near(const Vec3& a, const Vec3& b, float e = 1e-4f) { return std::abs(a.x - b.x) < e && std::abs(a.y - b.y) < e && std::abs(a.z - b.z) < e; }
} // namespace

TEST(camera_view_puts_eye_at_origin_and_forward_down_minus_z) {
    core::Pose p;
    p.pos = {10, 20, 30};
    p.fwd = {1, 0, 0};
    p.up = {0, 1, 0};
    float m[16];
    core::cam::viewMatrix(p, m);
    CHECK(near(apply(m, p.pos), {0, 0, 0}));                       // the eye is the origin
    CHECK(near(apply(m, p.pos + p.fwd * 5.0f), {0, 0, -5}));       // 5 ahead = 5 down -Z (OpenGL convention)
    CHECK(near(apply(m, p.pos + p.up * 2.0f), {0, 2, 0}));         // up stays up
    CHECK(near(apply(m, p.pos + Vec3{0, 0, 3}), {3, 0, 0}));       // facing +x with up +y, +z is to the right (+x in view space)
}

TEST(camera_default_pose_matches_identity_view) {
    core::Pose p;   // at origin, looking -Z, up +Y == OpenGL's default camera
    float m[16];
    core::cam::viewMatrix(p, m);
    for (int i = 0; i < 16; i++) CHECK(std::abs(m[i] - (i % 5 == 0 ? 1.0f : 0.0f)) < 1e-6f);
}

TEST(camera_chase_sits_behind_and_above_and_looks_at_the_ship) {
    core::Pose ship;
    ship.pos = {100, 0, 0};
    ship.fwd = {1, 0, 0};
    ship.up = {0, 1, 0};
    core::Pose c = core::cam::chasePose(ship, 12.0f, 3.5f);
    CHECK(near(c.pos, {88, 3.5f, 0}));                              // 12 behind, 3.5 up
    float m[16];
    core::cam::viewMatrix(c, m);
    Vec3 shipInView = apply(m, ship.pos);
    CHECK(shipInView.z < -1.0f);                                    // in front of the camera
    CHECK(std::abs(shipInView.x) < 1e-3f);                          // horizontally centred
    CHECK(shipInView.y < 0.0f);                                     // below centre: the ship sits low in frame
    CHECK(std::abs(engine::length(c.fwd) - 1.0f) < 1e-4f);
}

TEST(camera_view_survives_unnormalised_input) {
    core::Pose p;
    p.pos = {1, 2, 3};
    p.fwd = {0, 0, -10};          // not unit length
    p.up = {0, 5, 0};
    float m[16];
    core::cam::viewMatrix(p, m);
    CHECK(near(apply(m, p.pos + Vec3{0, 0, -4}), {0, 0, -4}));
}

// ---- chase camera: a rigid offset camera in the ship frame ----
namespace {
core::Pose rolledPose() {
    core::Pose p;
    p.pos = {40000, -300, 12000};
    p.fwd = engine::normalize(Vec3{0.5f, 0.3f, -0.81f});
    Vec3 up0 = engine::normalize(Vec3{0, 1, 0} - p.fwd * engine::dot(Vec3{0, 1, 0}, p.fwd));
    Vec3 r0 = engine::cross(p.fwd, up0);
    p.up = up0 * std::cos(0.61f) + r0 * std::sin(0.61f);           // rolled 35 degrees
    return p;
}
float rot3(const float m[16], int r, int c) { return m[c * 4 + r]; }
}

TEST(chase_camera_has_the_ships_orientation_exactly) {
    core::Pose ship = rolledPose();
    core::Pose c = core::cam::chasePose(ship, 24.0f, 6.0f);
    CHECK(near(c.fwd, engine::normalize(ship.fwd)));                // looks parallel to the ship's forward: no look-at pitch
    Vec3 shipUp = engine::normalize(ship.up);
    CHECK(near(c.up, shipUp, 1e-3f));                               // roll follows the ship
    // the VIEW ROTATION (what the skybox and stars are drawn with) is identical in cockpit and chase view: only the translation differs
    float mShip[16], mChase[16];
    core::cam::viewMatrix(ship, mShip);
    core::cam::viewMatrix(c, mChase);
    for (int r = 0; r < 3; r++) for (int col = 0; col < 3; col++) CHECK(std::abs(rot3(mShip, r, col) - rot3(mChase, r, col)) < 1e-4f);
}
TEST(chase_camera_offset_is_measured_in_the_ship_frame) {
    core::Pose ship = rolledPose();
    core::Pose c = core::cam::chasePose(ship, 24.0f, 6.0f);
    Vec3 d = c.pos - ship.pos;
    Vec3 f = engine::normalize(ship.fwd), u = engine::normalize(ship.up);
    CHECK(std::abs(engine::dot(d, f) + 24.0f) < 1e-2f);             // 24 behind along the ship's forward
    CHECK(std::abs(engine::dot(d, u) - 6.0f) < 1e-2f);              // 6 above along the ship's up (not world up)
    CHECK(std::abs(engine::dot(d, engine::cross(f, u))) < 1e-2f);   // none sideways
}
TEST(chase_camera_never_produces_nan) {
    core::Pose ship;
    ship.pos = {1, 2, 3};
    for (Vec3 f : {Vec3{0, 0, 0}, Vec3{0, 1, 0}, Vec3{0, -1, 0}, Vec3{std::nanf(""), 0, 0}, Vec3{0, 0, -1e-30f}}) {
        for (Vec3 u : {Vec3{0, 0, 0}, Vec3{0, 1, 0}, f, Vec3{0, 0, 1}}) {
            ship.fwd = f; ship.up = u;
            core::Pose c = core::cam::chasePose(ship, 12.0f, 3.5f);
            float m[16];
            core::cam::viewMatrix(c, m);
            for (int i = 0; i < 16; i++) CHECK(std::isfinite(m[i]));
            CHECK(std::abs(engine::length(c.fwd) - 1.0f) < 1e-4f && std::abs(engine::length(c.up) - 1.0f) < 1e-4f);
            CHECK(std::abs(engine::dot(c.fwd, c.up)) < 1e-4f);
        }
    }
    core::cam::Frame fr = core::cam::orthoFrame({0, 1, 0}, {0, 1, 0});   // straight up with up along forward (pitch +90)
    CHECK(std::abs(engine::dot(fr.fwd, fr.up)) < 1e-5f && std::abs(engine::length(fr.right) - 1.0f) < 1e-4f);
}
TEST(chase_camera_old_lookat_pitched_the_view_down_about_ten_degrees) {
    // documents the bug: aiming at a point 0.6 * distance ahead tilts the view by atan(height / (1.6 * distance)) relative to the ship
    float pitch = std::atan(3.5f / (12.0f * 1.6f)) * 57.29578f;
    CHECK(pitch > 10.0f && pitch < 10.5f);
}
