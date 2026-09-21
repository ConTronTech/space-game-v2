#pragma once
// Pure maths for render.scale (no GL): the 3D world is drawn into an offscreen buffer at scale x the window size and stretched to the window.
#include <algorithm>
#include <cmath>

namespace core {

constexpr float kMinRenderScale = 0.5f;

inline float clampRenderScale(float s) {
    if (!(s == s)) return 1.0f;                                  // NaN
    return std::clamp(s, kMinRenderScale, 1.0f);
}

struct ScaledSize {
    int w = 0, h = 0;      // size to render the world at
    bool scaled = false;   // false: render straight to the window (no offscreen buffer at all)
};

// fboAvailable = the driver has framebuffer objects. Scale >= ~1 or no FBO = the untouched, zero-cost path.
inline ScaledSize scaledSize(int windowW, int windowH, float scale, bool fboAvailable) {
    float s = clampRenderScale(scale);
    if (!fboAvailable || s >= 0.995f || windowW < 2 || windowH < 2) return {windowW, windowH, false};
    return {std::max(16, (int)std::lround((double)windowW * s)), std::max(16, (int)std::lround((double)windowH * s)), true};
}

// Should the frame's colour clear run? 'tunable' = render.clear_color (default true). It may be skipped only when something guarantees every pixel is
// overwritten anyway: the skybox covers the whole view, or the world buffer is stretched over the whole window.
inline bool colorClearNeeded(bool tunable, bool sceneCoversScreen, bool worldBufferActive) {
    return tunable || !(sceneCoversScreen || worldBufferActive);
}

} // namespace core
