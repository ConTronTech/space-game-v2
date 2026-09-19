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

// Chase camera: behind and above the ship, looking at a point ahead of it so the ship sits low in frame.
inline Pose chasePose(const Pose& ship, float distance, float height) {
    using namespace engine;
    Vec3 f = normalize(ship.fwd), u = normalize(ship.up);
    Pose c;
    c.pos = ship.pos - f * distance + u * height;
    Vec3 target = ship.pos + f * (distance * 0.6f);
    c.fwd = normalize(target - c.pos);
    Vec3 r = normalize(cross(c.fwd, u));
    c.up = cross(r, c.fwd);
    return c;
}

} // namespace core::cam
