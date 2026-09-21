#pragma once
// Pure layout math for the tabs widget (no SDL/GL): equal-width tabs across a row, hit testing, cycling. Unit-tested in test_game_menu.cpp.
#include <algorithm>

namespace core {

struct TabRect { float x = 0, w = 0; };

// Tab i of n across a row starting at x, total width w, with `gap` pixels between tabs. Tabs share the width equally.
inline TabRect tabRect(int i, int n, float x, float w, float gap = 6.0f) {
    if (n <= 0 || i < 0 || i >= n) return {};
    float tw = (w - gap * (float)(n - 1)) / (float)n;
    return {x + (float)i * (tw + gap), std::max(0.0f, tw)};
}

// Which tab a pixel column px belongs to, -1 if it is outside the row or in a gap.
inline int tabAt(float px, int n, float x, float w, float gap = 6.0f) {
    for (int i = 0; i < n; i++) { TabRect r = tabRect(i, n, x, w, gap); if (px >= r.x && px <= r.x + r.w) return i; }
    return -1;
}

// Next / previous tab, wrapping (dir > 0 next, else previous). n <= 0 gives 0.
inline int cycleTab(int selected, int n, int dir) {
    if (n <= 0) return 0;
    return ((selected + (dir >= 0 ? 1 : -1)) % n + n) % n;
}

// A selected index that is out of range (tabs came and went) is pulled back into 0..n-1.
inline int clampTab(int selected, int n) { return n <= 0 ? 0 : std::clamp(selected, 0, n - 1); }

} // namespace core
