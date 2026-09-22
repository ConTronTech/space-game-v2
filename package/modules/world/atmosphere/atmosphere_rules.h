#pragma once
// Pure planet-atmosphere rules: no GL, no SDL, no engine types. Unit-tested in package/tests/test_atmosphere.cpp. Design in docs/ATMOSPHERE.md.
//   * rim (Fresnel-style) alpha from the cosine between a shell vertex's normal and the direction to the camera
//   * a day/night factor from the cosine between the normal and the direction to the sun
//   * shell radius, visibility range and the distance fade
//   * the "inside the atmosphere" screen tint as a function of the camera's depth into the shell
#include <algorithm>
#include <cmath>

namespace world {

struct AtmosphereParams {
    float shellFactor = 1.06f;     // shell radius / body radius (terrain peaks reach 1.03, so the shell always clears them)
    float rangeFactor = 12.0f;     // drawn while the camera is within this many body radii of the centre
    float fadeStart = 0.7f;        // fraction of the range where the distance fade starts (fully faded at the range)
    float rimPower = 3.0f;         // exponent of the rim curve: higher = thinner, sharper limb
    float rimAlpha = 0.85f;        // alpha at a grazing (edge-on) vertex
    float nightFloor = 0.15f;      // share of the glow kept on the night side
    float insideTintMax = 0.12f;   // screen tint alpha at the surface (0 at the shell boundary)
};

// Rim alpha from c = dot(normal, direction to camera). |c| near 0 (edge-on: the limb from outside, the horizon from inside) = bright;
// |c| near 1 (looking straight down at, or straight up through, the shell) = nearly clear. Using |c| makes the same curve right from
// outside (c > 0 on the near face) and from inside (c < 0 overhead). Clamped to [0, maxAlpha].
inline float rimAlpha(float c, float power, float maxAlpha) {
    float x = 1.0f - std::min(1.0f, std::fabs(c));
    return std::clamp(maxAlpha * std::pow(x, std::max(0.1f, power)), 0.0f, maxAlpha);
}

// Day side glows, night side keeps a faint floor; a soft terminator (the glow wraps a little past it, as real twilight does).
inline float sunFactor(float nDotSun, float nightFloor) {
    float t = std::clamp((nDotSun + 0.8f) / 1.4f, 0.0f, 1.0f);      // full day from 0.6, the night floor below -0.8
    t = t * t * (3.0f - 2.0f * t);
    return nightFloor + (1.0f - nightFloor) * t;
}

inline float shellRadius(float bodyRadius, float shellFactor) { return bodyRadius * std::max(1.0f, shellFactor); }
inline double visibleRange(float bodyRadius, float rangeFactor) { return (double)bodyRadius * std::max(1.0f, rangeFactor); }

// 1 inside fadeStart * range, falling smoothly to 0 at the range (no pop when a planet enters or leaves the drawn set).
inline float distanceFade(double dist, double range, float fadeStart) {
    if (range <= 0) return 0.0f;
    double a = range * std::clamp((double)fadeStart, 0.0, 0.999);
    if (dist <= a) return 1.0f;
    if (dist >= range) return 0.0f;
    float t = (float)((range - dist) / (range - a));
    return t * t * (3.0f - 2.0f * t);
}

// Screen tint when the camera is inside the shell: 0 at the boundary (so crossing it never pops), rising smoothly to maxAlpha at the
// surface (more air above you = thicker haze). Outside the shell: 0.
inline float insideTint(double camDist, float bodyRadius, float shellR, float maxAlpha) {
    if (camDist >= shellR || shellR <= bodyRadius) return 0.0f;
    float depth = (float)std::clamp((shellR - camDist) / (double)(shellR - bodyRadius), 0.0, 1.0);
    return maxAlpha * depth * (2.0f - depth);    // ease-out: most of the haze arrives in the upper part of the shell
}

// Blue-shifted atmosphere colour from the body colour (the old game's formula, kept: it gives every planet a related but airy tint).
inline void atmosphereColor(const float* body, float* out) {
    out[0] = std::clamp(body[0] * 0.4f + 0.2f, 0.0f, 1.0f);
    out[1] = std::clamp(body[1] * 0.4f + 0.3f, 0.0f, 1.0f);
    out[2] = std::clamp(body[2] * 0.3f + 0.5f, 0.0f, 1.0f);
}

// Shell vertex count for a UV sphere of `slices` x `stacks` (with duplicated seam/pole vertices, like SphereMesh).
inline int shellVertexCount(int slices, int stacks) { return (slices + 1) * (stacks + 1); }

} // namespace world
