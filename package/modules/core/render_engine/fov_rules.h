#pragma once
// Field of view for any aspect ratio (pure, tested in tests/test_display.cpp). The setting (`video.fov`, default 90) is a VERTICAL field of view at 16:9.
//   camera.fov_mode = vertical : use it as is at every aspect (a 5:4 screen sees much less to the sides, an ultrawide much more)
//   camera.fov_mode = horplus  : (default) the horizontal FOV of the 16:9 reference is the target:
//        narrower than 16:9 (16:10, 4:3, 5:4): the horizontal FOV is held, so the vertical FOV grows: nothing is cropped or zoomed in at 5:4
//        wider than 16:9: the vertical FOV is held (classic Hor+), but the horizontal FOV is capped at kMaxHorizontalDeg (a 32:9 screen would otherwise fisheye)
// Always returns the vertical FOV to give gluPerspective, clamped to 30..140 like the setting.
#include <algorithm>
#include <cmath>
#include <string>

namespace core {

constexpr float kFovRefAspect = 16.0f / 9.0f;
constexpr float kMaxHorizontalDeg = 140.0f;

inline float horizontalFov(float verticalDeg, float aspect) {
    return 2.0f * std::atan(std::tan(verticalDeg * 3.14159265f / 360.0f) * aspect) * 180.0f / 3.14159265f;
}
inline float verticalFromHorizontal(float horizontalDeg, float aspect) {
    return 2.0f * std::atan(std::tan(horizontalDeg * 3.14159265f / 360.0f) / aspect) * 180.0f / 3.14159265f;
}

enum class FovMode { Vertical, HorPlus };
inline FovMode parseFovMode(const std::string& s) { return s == "vertical" ? FovMode::Vertical : FovMode::HorPlus; }

inline float effectiveVerticalFov(FovMode mode, float settingVerticalDeg, float aspect) {
    float base = std::clamp(settingVerticalDeg, 30.0f, 140.0f);
    if (!(aspect > 0.1f) || mode == FovMode::Vertical) return base;
    if (aspect >= kFovRefAspect) {
        float cap = std::max(kMaxHorizontalDeg, horizontalFov(base, kFovRefAspect));   // never below what the 16:9 reference itself already shows (identity at 16:9)
        float h = horizontalFov(base, aspect);
        return h > cap ? std::clamp(verticalFromHorizontal(cap, aspect), 30.0f, 140.0f) : base;
    }
    return std::clamp(verticalFromHorizontal(horizontalFov(base, kFovRefAspect), aspect), 30.0f, 140.0f);
}

} // namespace core
