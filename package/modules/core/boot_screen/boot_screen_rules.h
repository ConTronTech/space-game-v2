#pragma once
// Pure boot-progress-bar geometry: no GL/SDL/engine types. Unit-tested in package/tests/test_boot_screen.cpp.
#include <algorithm>

namespace core::boot {

struct Rect { float x = 0, y = 0, w = 0, h = 0; };

// The bar's track (background) rectangle: centred horizontally, a fixed fraction of the window width, near the bottom third
// (out of the way of anything a module's own early frames might draw). Degenerate window sizes (0 or negative) give an empty rect.
inline Rect trackRect(int winW, int winH) {
    if (winW <= 0 || winH <= 0) return {};
    float w = (float)winW * 0.5f, h = std::max(4.0f, (float)winH * 0.012f);
    return {((float)winW - w) * 0.5f, (float)winH * 0.72f, w, h};
}

// Fraction of modules loaded so far, clamped to [0,1]; total <= 0 (nothing to load yet) is 0, not a divide-by-zero.
inline float progressFraction(int index, int total) {
    if (total <= 0) return 0.0f;
    return std::clamp((float)index / (float)total, 0.0f, 1.0f);
}

// Same, while module number index+1 is still inside its own long job (engine::BootStep), `sub` (0..1) of the way through:
// the bar moves on within that module's slice instead of sitting still. Never past (index+1)/total, never backwards of index/total.
inline float progressFraction(int index, int total, float sub) {
    if (total <= 0) return 0.0f;
    return std::clamp(((float)index + std::clamp(sub, 0.0f, 1.0f)) / (float)total, 0.0f, 1.0f);
}

// The fill rectangle inside `track`, scaled by `fraction` (grows left to right), never wider than the track itself.
inline Rect fillRect(const Rect& track, float fraction) {
    Rect r = track;
    r.w = track.w * std::clamp(fraction, 0.0f, 1.0f);
    return r;
}

// Top-left of the "loading X (n/total)" label: left-aligned with the track, sitting just above it. `textHeight` is the
// glyph cell height (world/cockpit::textAdvance-style units, i.e. pixels here); degenerate track -> {0,0} (caller already
// skips drawing anything for an empty track).
inline Rect labelPos(const Rect& track, float textHeight) {
    if (track.w <= 0 || track.h <= 0) return {};
    return {track.x, track.y - textHeight * 1.4f, 0, 0};
}

// Whether THIS draw is still live, and whether the state should stop drawing AFTER it. `alreadyDone` short-circuits (once
// stopped, stays stopped); `reachedTotal` (the last module's ModuleLoaded already arrived) means: draw one more time at
// 100%, THEN stop - never skip that final full frame, and never draw again after it.
struct DrawDecision { bool draw = false, stopAfter = false; };
inline DrawDecision nextDraw(bool alreadyDone, bool reachedTotal) {
    if (alreadyDone) return {false, true};
    return {true, reachedTotal};
}

} // namespace core::boot
