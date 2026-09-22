#pragma once
// ui/toast - pure logic (no GL/SDL/engine types): the toast stack/queue state machine, fade timing, level colours, layout.
// Unit-tested in package/tests/test_toast.cpp. See docs/TOAST.md.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>

namespace toast {

enum class Level { Info, Warning, Urgent };

struct RGB { float r = 1, g = 1, b = 1; };

// The colour language the rest of the HUD already uses (ship_hud's kCalm cyan / amber / red), copied here on purpose: this module is decoupled.
inline RGB levelColor(Level l) {
    switch (l) {
        case Level::Warning: return {1.00f, 0.75f, 0.20f};   // amber
        case Level::Urgent:  return {1.00f, 0.25f, 0.22f};   // red
        default:             return {0.45f, 0.85f, 1.00f};   // calm cyan
    }
}

constexpr float kFadeSeconds = 0.5f;

// Lifetime alpha: 1 while fresh, then linear to 0 over the last kFadeSeconds (the same fade the HUD banners use). 'age' seconds since shown.
inline float fadeAlpha(double age, float lifetime) {
    if (age < 0 || lifetime <= 0 || age >= lifetime) return age < 0 ? 1.0f : 0.0f;
    float fade = std::min(kFadeSeconds, lifetime);
    double left = lifetime - age;
    return left >= fade ? 1.0f : (float)(left / fade);
}

// Urgent toasts pulse 0.55 .. 1 (about 1.4 Hz, like the HUD's warning banners); the others are steady.
inline float flash(Level l, double now) {
    if (l != Level::Urgent) return 1.0f;
    return 0.775f + 0.225f * std::sin((float)now * 9.0f);
}

// Layout: toasts sit in the BOTTOM-RIGHT corner, NEWEST AT THE BOTTOM; older ones are pushed up.
// Returns the top y of visible toast 'i' (0 = oldest visible) of 'n', for a screen 'screenH' tall.
inline float slotY(int i, int n, float screenH, float margin, float h, float gap) {
    int fromBottom = n - 1 - i;                      // newest (i = n-1) is slot 0 from the bottom
    return screenH - margin - h - fromBottom * (h + gap);
}
inline float slotX(float boxW, float screenW, float margin) { return screenW - margin - boxW; }

// Fixed-capacity FIFO of toasts. The oldest 'maxVisible' pending toasts are visible; the rest wait (they do not age while waiting).
// When a visible toast expires the next waiting one appears. When the whole queue is full, the OLDEST WAITING toast is dropped
// (never a visible one, so what the player is reading does not vanish); with no waiting toast (maxVisible == kCapacity) the oldest visible goes.
struct Entry {
    Level level = Level::Info;
    std::string text;
    float lifetime = 4.0f;
    double shownAt = -1;      // < 0 = waiting, not on screen yet
    float width = -1;         // pixel width cache for the renderer (-1 = not measured)
};

class Stack {
public:
    static constexpr int kCapacity = 16;

    explicit Stack(int maxVisible = 4) { setMaxVisible(maxVisible); }
    void setMaxVisible(int n) { maxVisible_ = std::clamp(n, 1, kCapacity); }
    int maxVisible() const { return maxVisible_; }

    void add(Level l, const std::string& text, float lifetime, double now) {
        if (count_ == kCapacity) {
            int drop = count_ > maxVisible_ ? maxVisible_ : 0;   // oldest waiting, else oldest visible
            eraseAt(drop);
            dropped_++;
        }
        Entry& e = slot(count_++);
        e.level = l; e.text = text; e.lifetime = lifetime > 0 ? lifetime : 0.1f; e.shownAt = -1; e.width = -1;
        promote(now);
    }

    // Removes expired visible toasts and shows waiting ones in their place. Call once per frame.
    void update(double now) {
        for (int i = 0; i < std::min(count_, maxVisible_);) {
            Entry& e = slot(i);
            if (e.shownAt >= 0 && now - e.shownAt >= e.lifetime) eraseAt(i);
            else i++;
        }
        promote(now);
    }

    void clear() { head_ = 0; count_ = 0; }

    int size() const { return count_; }                                  // visible + waiting
    int visible() const { return std::min(count_, maxVisible_); }
    int waiting() const { return count_ - visible(); }
    unsigned long dropped() const { return dropped_; }
    Entry& at(int i) { return slot(i); }                                 // 0 = oldest
    const Entry& at(int i) const { return ring_[(size_t)((head_ + i) % kCapacity)]; }
    float alpha(int i, double now) const { const Entry& e = at(i); return e.shownAt < 0 ? 0.0f : fadeAlpha(now - e.shownAt, e.lifetime); }

private:
    Entry& slot(int i) { return ring_[(size_t)((head_ + i) % kCapacity)]; }
    void promote(double now) { for (int i = 0; i < visible(); i++) if (slot(i).shownAt < 0) slot(i).shownAt = now; }
    // shift later entries down by one (swap keeps the strings' buffers: no allocation)
    void eraseAt(int i) {
        if (i == 0) { head_ = (head_ + 1) % kCapacity; count_--; return; }
        for (int k = i; k < count_ - 1; k++) std::swap(slot(k), slot(k + 1));
        count_--;
    }

    std::array<Entry, kCapacity> ring_{};
    int head_ = 0, count_ = 0, maxVisible_ = 4;
    unsigned long dropped_ = 0;
};

} // namespace toast
