#pragma once
// Pure starfield rules: no SDL, no GL, no engine types. Unit-tested in package/tests/test_world.cpp.
#include <algorithm>
#include <cmath>
#include <vector>

namespace world {

// Length of a warp streak in world units: 0 when not warping (or disabled), proportional to speed / refSpeed, capped at fullLength.
inline float warpStreakLength(bool warping, float speed, float refSpeed, float fullLength) {
    if (!warping || fullLength <= 0.0f || refSpeed <= 0.0f || speed <= 0.0f) return 0.0f;
    return fullLength * std::min(1.0f, speed / refSpeed);
}

// World-space half-size of a camera-facing square star that is 'pixels' wide on screen when seen from the origin at 'distance':
// one pixel is 2 * tan(fov / 2) * distance / viewportHeight world units at that distance. Never smaller than one pixel (it would flicker).
inline float starQuadHalfSize(float pixels, float distance, float fovYDeg, float viewportHeightPx) {
    if (viewportHeightPx < 1.0f || distance <= 0.0f) return 0.0f;
    float px = std::max(1.0f, pixels);
    float worldPerPixel = 2.0f * std::tan(fovYDeg * 0.5f * 3.14159265f / 180.0f) * distance / viewportHeightPx;
    return px * 0.5f * worldPerPixel;
}

// Fills 'out' (xyz per vertex, 4 vertices per star) with squares centred on dir * radius, facing the origin. dirs = unit vectors, xyz per star.
inline void buildStarQuads(const std::vector<float>& dirs, float radius, float halfSize, std::vector<float>& out) {
    out.resize(dirs.size() / 3 * 12);
    for (size_t s = 0; s + 2 < dirs.size(); s += 3) {
        float dx = dirs[s], dy = dirs[s + 1], dz = dirs[s + 2];
        float ux = 0, uy = 1, uz = 0;                                   // reference up; use another axis near the poles
        if (std::fabs(dy) > 0.95f) { ux = 1; uy = 0; }
        float rx = dy * uz - dz * uy, ry = dz * ux - dx * uz, rz = dx * uy - dy * ux;
        float rl = std::sqrt(rx * rx + ry * ry + rz * rz);
        rx /= rl; ry /= rl; rz /= rl;
        float vx = dy * rz - dz * ry, vy = dz * rx - dx * rz, vz = dx * ry - dy * rx;   // second axis, perpendicular to dir and r
        float* o = &out[s / 3 * 12];
        const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
        for (int k = 0; k < 4; k++) {
            o[k * 3 + 0] = dx * radius + (rx * sx[k] + vx * sy[k]) * halfSize;
            o[k * 3 + 1] = dy * radius + (ry * sx[k] + vy * sy[k]) * halfSize;
            o[k * 3 + 2] = dz * radius + (rz * sx[k] + vz * sy[k]) * halfSize;
        }
    }
}

} // namespace world
