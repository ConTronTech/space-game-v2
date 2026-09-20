#pragma once
// Pure HUD logic: thresholds, colours, layout and banner timers. No SDL/GL, so it is unit-tested (tests/test_hud_logic.cpp).
// Times are passed in (seconds), never read from a clock.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace hud {

struct RGB { float r = 1, g = 1, b = 1; };

inline float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }
inline float fraction(float cur, float max) { return max > 0 ? clamp01(cur / max) : 0.0f; }
inline RGB lerp(const RGB& a, const RGB& b, float t) { t = clamp01(t); return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

constexpr float kLowHp = 0.25f, kLowFuel = 0.15f;
constexpr float kImpactSeconds = 0.7f, kEventBannerSeconds = 3.0f;

enum class Bar { Hp, Shield, Fuel };

// hull: green -> amber (50%) -> red (0%). Shield (blue) and fuel (violet) hold their colour down to 30%, then go amber -> red.
inline RGB barColor(Bar kind, float frac) {
    const RGB amber{1.00f, 0.75f, 0.20f}, red{1.00f, 0.25f, 0.22f};
    RGB high = kind == Bar::Hp ? RGB{0.30f, 0.90f, 0.42f} : kind == Bar::Shield ? RGB{0.35f, 0.70f, 1.00f} : RGB{0.55f, 0.60f, 1.00f};
    frac = clamp01(frac);
    if (kind == Bar::Hp) {
        if (frac >= 0.5f) return lerp(amber, high, (frac - 0.5f) / 0.5f);
        return lerp(red, amber, frac / 0.5f);
    }
    // shield / fuel keep their own colour until nearly empty (blending blue with amber would look grey)
    if (frac >= 0.3f) return high;
    if (frac >= 0.15f) return lerp(amber, high, (frac - 0.15f) / 0.15f);
    return lerp(red, amber, frac / 0.15f);
}

// ---- layout (pixels, origin top-left). Everything scales with the window; 1.0 at 1280x720. ----
inline float uiScale(int w, int h) { return std::min(3.0f, std::max(0.6f, std::min(w / 1280.0f, h / 720.0f))); }

struct Rect { float x = 0, y = 0, w = 0, h = 0; float bottom() const { return y + h; } float right() const { return x + w; } };

struct Layout {
    float scale = 1;
    Rect speed, bars;          // top-left stack
    float barRowH = 0, barPad = 0;
    Rect hint;                 // bottom centre (width is set by the caller from the text, see fitHint)
    float hintFont = 13;
    float bannerY = 0, bannerW = 0, bannerH = 0, bannerGap = 0;   // banners: centred, near the top
    float crossR = 0;          // crosshair arm length
    float cx = 0, cy = 0;
};

// rows = number of bars shown (2 or 3).
inline Layout computeLayout(int w, int h, int rows) {
    Layout L; float s = L.scale = uiScale(w, h);
    float m = 16 * s;
    L.speed = {m, m, 200 * s, 58 * s};
    L.barRowH = 26 * s; L.barPad = 10 * s;
    L.bars = {m, L.speed.bottom() + 8 * s, 200 * s, L.barPad * 2 + L.barRowH * rows};
    L.hintFont = std::round(13 * s);
    L.hint = {0, h - 52 * s, 0, 34 * s};
    L.bannerY = m; L.bannerW = 300 * s; L.bannerH = 40 * s; L.bannerGap = 8 * s;
    L.crossR = 10 * s;
    L.cx = w / 2.0f; L.cy = h / 2.0f;
    return L;
}

// Sets the hint panel's x/w from its text width (at L.hintFont). If it does not fit the screen, returns a smaller font size.
inline int fitHint(Layout& L, int screenW, const std::function<int(const std::string&, int)>& textWidth, const std::string& text) {
    int font = (int)L.hintFont;
    float pad = 40.0f * L.scale;
    while (font > 8 && textWidth(text, font) + pad > screenW - 2 * 16 * L.scale) font--;
    float w = textWidth(text, font) + pad;
    L.hint.w = w; L.hint.x = (screenW - w) / 2;
    return font;
}

// ---- warnings ----
enum class Warn { LowHp, LowFuel, FuelEmpty, ShieldBroken };
struct Banner { Warn kind; std::string text; float alpha; };   // alpha: flashing 0.55..1, times the fade-out of timed ones

struct Snapshot { float hp = 100, maxHp = 100, warpFuel = 100, maxWarpFuel = 100; bool alive = true; };

class HudState {
public:
    // amount = what reached the hull, absorbedByShield = what the shield took (ship::DamageTaken); the HUD shows the total.
    void onDamage(float amount, float absorbedByShield, const std::string& source, double now) {
        impactAmount_ = amount + absorbedByShield; impactShielded_ = absorbedByShield > 0; impactSource_ = source; impactAt_ = now;
    }
    void onShieldBroken(double now) { shieldBrokenAt_ = now; }
    void onFuelEmpty(double now) { fuelEmptyAt_ = now; }
    void onRespawned() { impactAt_ = shieldBrokenAt_ = fuelEmptyAt_ = kNever; }

    // 0 (none) .. 1 (just hit); fades linearly over kImpactSeconds
    float impactAlpha(double now) const { return timedAlpha(impactAt_, now, kImpactSeconds); }
    float impactAmount() const { return impactAmount_; }        // total hit: hull + shield part
    bool impactShielded() const { return impactShielded_; }     // the shield took any of it
    // 0.3 .. 1: how strong the red edge is (a 40+ hit is full strength)
    float impactSeverity() const { return clamp01(0.3f + impactAmount_ / 40.0f * 0.7f); }
    const std::string& impactSource() const { return impactSource_; }

    // Active warning banners, most urgent first. A dead ship shows none (the destroyed screen replaces them).
    std::vector<Banner> banners(const Snapshot& s, double now) const {
        std::vector<Banner> out;
        if (!s.alive) return out;
        float flash = 0.775f + 0.225f * std::sin((float)now * 9.0f);
        if (fraction(s.hp, s.maxHp) <= kLowHp) out.push_back({Warn::LowHp, "LOW HULL INTEGRITY", flash});
        if (float a = timedAlpha(shieldBrokenAt_, now, kEventBannerSeconds); a > 0) out.push_back({Warn::ShieldBroken, "SHIELD BROKEN", flash * std::min(1.0f, a * 3)});
        if (float a = timedAlpha(fuelEmptyAt_, now, kEventBannerSeconds); a > 0) out.push_back({Warn::FuelEmpty, "WARP FUEL EMPTY", flash * std::min(1.0f, a * 3)});
        else if (s.maxWarpFuel > 0 && s.warpFuel > 0 && fraction(s.warpFuel, s.maxWarpFuel) <= kLowFuel) out.push_back({Warn::LowFuel, "LOW WARP FUEL", flash});
        return out;
    }

private:
    static constexpr double kNever = -1e9;
    static float timedAlpha(double at, double now, float dur) {
        double age = now - at;
        return (age < 0 || age >= dur) ? 0.0f : (float)(1.0 - age / dur);
    }
    double impactAt_ = kNever, shieldBrokenAt_ = kNever, fuelEmptyAt_ = kNever;
    float impactAmount_ = 0;
    bool impactShielded_ = false;
    std::string impactSource_;
};

// ---- cockpit overlay mode (hud.cockpit_overlay) ----
// While the cockpit model's own screens show speed/bars, the flat overlay is trimmed. When the default UI is shown
// (chase view, no screens, no cockpit) everything is drawn whatever the mode says.
enum class OverlayMode { Minimal, Full, Hidden };
struct ParsedMode { OverlayMode mode = OverlayMode::Minimal; bool unknown = false; };

inline ParsedMode parseOverlayMode(const std::string& s) {
    if (s == "minimal") return {OverlayMode::Minimal, false};
    if (s == "full") return {OverlayMode::Full, false};
    if (s == "hidden") return {OverlayMode::Hidden, false};
    return {OverlayMode::Minimal, true};
}

struct OverlayPlan { bool speed = true, bars = true, crosshair = true, banners = true, vignette = true, hint = true, destroyed = true; };

inline OverlayPlan overlayPlan(OverlayMode mode, bool showsDefaultUI) {
    OverlayPlan p;                                         // everything on
    if (showsDefaultUI || mode == OverlayMode::Full) return p;
    p.speed = p.bars = false;                              // minimal: that data lives on the ship's screens
    if (mode == OverlayMode::Hidden) p.crosshair = p.banners = p.hint = false;
    return p;
}

// Controls hint opacity: 1 for the first 'seconds' of engine time, then linear 1 -> 0 over kHintFadeSeconds. seconds <= 0: always 1.
constexpr float kHintFadeSeconds = 2.0f;
inline float hintAlpha(double now, float seconds) {
    if (seconds <= 0) return 1.0f;
    if (now <= seconds) return 1.0f;
    return clamp01(1.0f - (float)((now - seconds) / kHintFadeSeconds));
}

inline std::string impactText(float amount, const std::string& source, bool shielded = false) {
    char b[96];
    std::snprintf(b, sizeof b, "IMPACT  %.0f", amount);
    std::string s = b;
    if (!source.empty()) { std::string up = source; for (auto& c : up) c = (char)std::toupper((unsigned char)c); s += "  " + up; }
    if (shielded) s += "  (SHIELD)";
    return s;
}

} // namespace hud
