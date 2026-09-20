#pragma once
// Small pure helpers for the cockpit's lighting tunables.
#include <cstdio>
#include <string>
#include "engine/math.h"

namespace cockpit {

// "0.3, 0.8, 0.5" -> Vec3. False (out untouched) unless exactly three numbers and a non-zero length.
inline bool parseVec3(const std::string& s, engine::Vec3& out) {
    float x, y, z;
    char extra;
    if (std::sscanf(s.c_str(), " %f , %f , %f %c", &x, &y, &z, &extra) != 3) return false;
    engine::Vec3 v{x, y, z};
    if (engine::length(v) < 1e-6f) return false;
    out = v;
    return true;
}

} // namespace cockpit
