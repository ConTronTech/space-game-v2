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
