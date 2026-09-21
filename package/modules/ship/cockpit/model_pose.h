#pragma once
// Pure maths for drawing the ship model and lighting it (no GL, unit-tested in tests/test_cockpit.cpp).
//
// The ShipV2 model is authored in VIEW space: the pilot's eye is the origin, +X right, +Y up, -Z forward, 1 unit = 1 metre. In cockpit view it is drawn
// with an identity modelview (glued to the camera). In chase view it is drawn in world space: modelview = view * shipToWorld, where shipToWorld has the columns
// (right, up, -forward, position) of the ship's pose (the same convention ship_core's wireframe fighter used).
#include <cmath>
#include "core/camera/camera_math.h"
#include "engine/math.h"

namespace cockpit {

// modelview = view * shipToWorld, computed in double: the ship can be 40,000+ units from the origin, where float rounding of view * translate would
// wobble the model. The camera position is recovered from the view matrix (cam = -R^T t), the model is placed relative to the camera.
inline void chaseModelView(const float view[16], const engine::Vec3& shipPos, const engine::Vec3& fwd, const engine::Vec3& up, float out[16]) {
    core::cam::Frame fr = core::cam::orthoFrame(fwd, up);
    // view rotation rows R[row][col] = view[col * 4 + row]; view translation t = (view[12], view[13], view[14])
    double R[3][3], t[3];
    for (int r = 0; r < 3; r++) { for (int c = 0; c < 3; c++) R[r][c] = view[c * 4 + r]; t[r] = view[12 + r]; }
    // world point p -> view point R p + t. The ship's origin in view space is R * shipPos + t (float cancellation avoided by using cam-relative below).
    double cam[3];
    for (int c = 0; c < 3; c++) cam[c] = -(R[0][c] * t[0] + R[1][c] * t[1] + R[2][c] * t[2]);
    double rel[3] = {shipPos.x - cam[0], shipPos.y - cam[1], shipPos.z - cam[2]};
    const double axes[3][3] = {{fr.right.x, fr.right.y, fr.right.z}, {fr.up.x, fr.up.y, fr.up.z}, {-fr.fwd.x, -fr.fwd.y, -fr.fwd.z}};   // ship local x, y, z in world
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 3; row++)
            out[col * 4 + row] = (float)(R[row][0] * axes[col][0] + R[row][1] * axes[col][1] + R[row][2] * axes[col][2]);
    for (int row = 0; row < 3; row++) out[12 + row] = (float)(R[row][0] * rel[0] + R[row][1] * rel[1] + R[row][2] * rel[2]);
    out[3] = out[7] = out[11] = 0; out[15] = 1;
}

// Unit vector from the ship toward the sun (world space), or (0,0,0) when they coincide.
struct Dir3 { double x = 0, y = 0, z = 0; };
inline Dir3 sunDirection(double sx, double sy, double sz, double px, double py, double pz) {
    double dx = sx - px, dy = sy - py, dz = sz - pz, l = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(l > 1e-9)) return {};
    return {dx / l, dy / l, dz / l};
}

// A world-space direction expressed in view space (rotation part of the view matrix only). GL stores a light's position in eye space when it is set, so
// this is what to pass to glLightfv(GL_POSITION) with w = 0 while the modelview is identity.
inline engine::Vec3 dirToViewSpace(const float view[16], const Dir3& d) {
    return {(float)(view[0] * d.x + view[4] * d.y + view[8] * d.z), (float)(view[1] * d.x + view[5] * d.y + view[9] * d.z),
            (float)(view[2] * d.x + view[6] * d.y + view[10] * d.z)};
}

} // namespace cockpit
