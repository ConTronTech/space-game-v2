#pragma once
// Pure camera math (unit-tested).
#include "core/camera/camera_api.h"

namespace core::cam {

// Column-major OpenGL view matrix for a camera at pose.pos looking along pose.fwd with pose.up above it.
inline void viewMatrix(const Pose& p, float m[16]) {
    using namespace engine;
    Vec3 f = normalize(p.fwd);
    Vec3 r = normalize(cross(f, p.up));
    Vec3 u = cross(r, f);
    m[0] = r.x;  m[4] = r.y;  m[8]  = r.z;  m[12] = -dot(r, p.pos);
    m[1] = u.x;  m[5] = u.y;  m[9]  = u.z;  m[13] = -dot(u, p.pos);
    m[2] = -f.x; m[6] = -f.y; m[10] = -f.z; m[14] = dot(f, p.pos);
    m[3] = 0;    m[7] = 0;    m[11] = 0;    m[15] = 1;
}

// An orthonormal frame from a forward and an up vector that survives bad input: a zero / non-finite forward becomes -Z, an up that is zero or
// parallel to forward becomes any perpendicular one (so pitch +-90 degrees and NaN never produce a NaN matrix).
struct Frame { engine::Vec3 fwd{0, 0, -1}, up{0, 1, 0}, right{1, 0, 0}; };
inline Frame orthoFrame(const engine::Vec3& fwd, const engine::Vec3& up) {
    using namespace engine;
    Frame fr;
    Vec3 f = normalize(fwd);
    if (!(length(f) > 0.5f)) f = {0, 0, -1};                       // zero (or NaN: comparisons fail) -> default
    Vec3 u = up - f * dot(up, f);
    if (!(length(u) > 1e-4f)) {                                     // up missing or along forward: pick the world axis least aligned with forward
        Vec3 axis = std::abs(f.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{0, 0, -1};
        u = axis - f * dot(axis, f);
    }
    u = normalize(u);
    fr.fwd = f; fr.up = u; fr.right = cross(f, u);
    return fr;
}

// Chase camera: a RIGID offset camera fixed to the ship frame. Its orientation IS the ship's (roll included), and it sits `distance` behind and
// `height` above the ship measured along the ship's own axes, looking parallel to the ship's forward. Everything in the world (skybox, stars,
// planets) is drawn with this same view, so the sky rotates exactly like the cockpit view's sky: toggling V only translates the eye.
// (It used to look at a point ahead of the ship, which pitched the camera ~10 degrees down relative to the ship and so rotated the whole sky.)
inline Pose chasePose(const Pose& ship, float distance, float height) {
    Frame fr = orthoFrame(ship.fwd, ship.up);
    Pose c;
    c.pos = ship.pos - fr.fwd * distance + fr.up * height;
    c.fwd = fr.fwd;
    c.up = fr.up;
    return c;
}

} // namespace core::cam
