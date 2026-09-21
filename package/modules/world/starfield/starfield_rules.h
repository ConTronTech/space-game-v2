#pragma once
// Pure starfield rules: no SDL, no GL, no engine types. Unit-tested in package/tests/test_world.cpp.
#include <algorithm>

namespace world {

// Length of a warp streak in world units: 0 when not warping (or disabled), proportional to speed / refSpeed, capped at fullLength.
inline float warpStreakLength(bool warping, float speed, float refSpeed, float fullLength) {
    if (!warping || fullLength <= 0.0f || refSpeed <= 0.0f || speed <= 0.0f) return 0.0f;
    return fullLength * std::min(1.0f, speed / refSpeed);
}

} // namespace world
