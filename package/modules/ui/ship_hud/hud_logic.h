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
constexpr float kImpactSeconds = 0.7f, kEventBannerSeconds = 3.0f, kOrbitReleasedSeconds = 2.5f, kDockBannerSeconds = 2.5f, kWeaponBannerSeconds = 2.0f, kHitMarkerSeconds = 0.25f, kKillMarkerSeconds = 0.45f,
                 kPickupSeconds = 1.5f, kPickupMergeSeconds = 0.5f, kCargoFullSeconds = 2.0f, kCargoFullMinGap = 2.0f;
constexpr int kMaxPickups = 4;
constexpr int kHeatSegments = 8;

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
enum class Warn { LowHp, LowFuel, FuelEmpty, ShieldBroken, OrbitReleased, Docked, Undocked, WeaponOverheated, WeaponChanged, CargoFull };
struct Banner { Warn kind; std::string text; float alpha; };   // alpha: flashing 0.55..1, times the fade-out of timed ones

struct Snapshot { float hp = 100, maxHp = 100, warpFuel = 100, maxWarpFuel = 100; bool alive = true; };

// "ORBIT LOCKED: PLANET 1"; just "ORBIT LOCKED" when the body has no name.
inline std::string orbitStatusText(const std::string& body) {
    if (body.empty()) return "ORBIT LOCKED";
    std::string up = body;
    for (auto& c : up) c = (char)std::toupper((unsigned char)c);
    return "ORBIT LOCKED: " + up;
}

// ---- weapons (combat::ICombat) ----
// heat 0..1: calm cyan -> amber (60%) -> red (100%)
inline RGB heatColor(float heat) {
    const RGB cyan{0.45f, 0.85f, 1.0f}, amber{1.0f, 0.75f, 0.2f}, red{1.0f, 0.25f, 0.22f};
    heat = clamp01(heat);
    return heat < 0.6f ? lerp(cyan, amber, heat / 0.6f) : lerp(amber, red, (heat - 0.6f) / 0.4f);
}
// how many of the crosshair heat-bar segments are lit: 0 for no heat, otherwise at least one
inline int heatSegments(float heat, int total = kHeatSegments) {
    heat = clamp01(heat);
    return heat <= 0.001f ? 0 : std::min(total, (int)std::ceil(heat * total - 1e-4f));
}
// flashing while locked out: 0.35 .. 1
inline float overheatFlash(double now) { return 0.675f + 0.325f * std::sin((float)now * 16.0f); }
// hit marker: fades linearly over 0.25 s after a hit (0.45 s after a kill); a kill also changes the colour
struct HitMarker { float alpha = 0; bool kill = false; };
inline HitMarker hitMarker(float hitAge, float killAge) {
    HitMarker m;
    if (killAge >= 0 && killAge < kKillMarkerSeconds) { m.alpha = 1.0f - killAge / kKillMarkerSeconds; m.kill = true; }
    if (hitAge >= 0 && hitAge < kHitMarkerSeconds) { float a = 1.0f - hitAge / kHitMarkerSeconds; if (a > m.alpha) { m.alpha = a; m.kill = false; } }
    return m;
}
inline std::string weaponLabel(const std::string& name, int index) {
    std::string up = name;
    for (auto& c : up) c = (char)std::toupper((unsigned char)c);
    return up + "  [" + std::to_string(index + 1) + "]";
}
enum class WeaponRow { Ready, Hot, Locked, Docked };   // how one weapon row of the weapon block looks
inline WeaponRow weaponRowState(bool busy, bool overheated, float heat) {
    if (busy) return WeaponRow::Docked;
    if (overheated) return WeaponRow::Locked;
    return heat >= 0.6f ? WeaponRow::Hot : WeaponRow::Ready;
}
inline const char* weaponRowNote(WeaponRow r) { return r == WeaponRow::Locked ? "OVERHEATED" : r == WeaponRow::Docked ? "DOCKED" : ""; }

// ---- cargo (gameplay::IInventory) and ore pickups (gameplay::OreMined) ----
inline float cargoFraction(float used, float capacity) { return capacity > 0 ? clamp01(used / capacity) : 0.0f; }
// calm cyan; amber above 80%; red when full
inline RGB cargoColor(float frac) {
    if (frac >= 0.999f) return {1.0f, 0.25f, 0.22f};
    if (frac > 0.8f) return {1.0f, 0.75f, 0.2f};
    return {0.45f, 0.85f, 1.0f};
}
inline std::string cargoText(float used, float capacity) {
    char b[48];
    std::snprintf(b, sizeof b, "CARGO %.0f / %.0f", used, capacity);
    return b;
}
// "+6 CRYSTAL": the ore's display name (data/ores.json) or, without it, its id, upper-cased
inline std::string pickupText(int amount, const std::string& label) { return "+" + std::to_string(amount) + " " + label; }
inline std::string oreLabel(const std::string& id, const std::string& dataName) {
    std::string n = dataName.empty() ? id : dataName;
    for (auto& c : n) c = (char)std::toupper((unsigned char)c);
    return n;
}

struct Pickup { std::string ore, label, text; RGB colour; int amount = 0; double at = -1e9; };   // 'at' = time of the latest addition

// ---- station docking prompt (ship::IDocking) ----
constexpr float kDockPromptFactor = 4.0f;       // prompt shows within this many dock radii (docking.prompt_range overrides, in units)
inline const char* dockKeyLabel() { return "G"; }   // core::IInput does not expose bindings: the default profile's key

inline std::string upper(std::string s) { for (auto& c : s) c = (char)std::toupper((unsigned char)c); return s; }

// "Station 1 (Planet 1, orbital)" -> "Station 1": the HUD line only needs the short name
inline std::string shortStationName(const std::string& name) {
    auto p = name.find(" (");
    return p == std::string::npos || p == 0 ? name : name.substr(0, p);
}

// "420 m", "1.2 km"
inline std::string distanceText(float meters) {
    char b[32];
    if (meters < 1000.0f) std::snprintf(b, sizeof b, "%.0f m", meters);
    else std::snprintf(b, sizeof b, "%.1f km", meters / 1000.0f);
    return b;
}
// prompt range: an explicit tunable (> 0) or kDockPromptFactor x the station's dock radius
inline float dockPromptRange(float dockRadius, float tunable) { return tunable > 0 ? tunable : kDockPromptFactor * dockRadius; }
inline bool inDockPromptRange(float distance, float dockRadius, float tunable) { return distance <= dockPromptRange(dockRadius, tunable); }

// ship/docking's reasons -> short forms; unknown reasons are shown as they are
inline std::string shortDockReason(const std::string& reason) {
    std::string r = reason;
    for (auto& c : r) c = (char)std::tolower((unsigned char)c);
    auto has = [&](const char* n) { return r.find(n) != std::string::npos; };
    if (has("too far")) return "too far";
    if (has("too fast")) return "too fast";
    if (has("warp")) return "warp drive on";
    if (has("orbit")) return "orbit lock on";
    return reason;
}

struct DockQuery { bool has = false, ok = false; float distance = 0; std::string name, reason; };
struct DockPrompt { bool show = false, ready = false; std::string status, action; };   // status: "STATION 1  420 m"; action: "DOCK [G]" or the short reason

// What to show while flying near a station. Nothing when there is no station, it is out of range, or we are docked.
inline DockPrompt dockPrompt(const DockQuery& q, float dockRadius, float tunableRange, bool docked) {
    DockPrompt p;
    if (docked || !q.has || !inDockPromptRange(q.distance, dockRadius, tunableRange)) return p;
    p.show = true;
    p.ready = q.ok;
    p.status = upper(shortStationName(q.name)) + "  " + distanceText(q.distance);
    p.action = q.ok ? std::string("DOCK [") + dockKeyLabel() + "]" : shortDockReason(q.reason);
    return p;
}
inline std::string dockedText(const std::string& station) {
    return "DOCKED: " + upper(shortStationName(station)) + " - [" + dockKeyLabel() + "] UNDOCK";
}

class HudState {
public:
    // amount = what reached the hull, absorbedByShield = what the shield took (ship::DamageTaken); the HUD shows the total.
    void onDamage(float amount, float absorbedByShield, const std::string& source, double now) {
        impactAmount_ = amount + absorbedByShield; impactShielded_ = absorbedByShield > 0; impactSource_ = source; impactAt_ = now;
    }
    void onShieldBroken(double now) { shieldBrokenAt_ = now; }
    void onFuelEmpty(double now) { fuelEmptyAt_ = now; }
    // ship::OrbitLockChanged. A release with no lock before it (e.g. a duplicate event) shows nothing.
    void onOrbitLock(bool locked, const std::string& body, double now) {
        if (locked) { orbitLocked_ = true; orbitBody_ = body; orbitText_ = orbitStatusText(body); return; }
        if (orbitLocked_) orbitReleasedAt_ = now;
        orbitLocked_ = false;
    }
    bool orbitLocked() const { return orbitLocked_; }
    const std::string& orbitStatus() const { return orbitLocked_ ? orbitText_ : empty_; }   // built once per event, not per frame
    // ship::Docked / Undocked
    void onDocked(const std::string& station, double now) { docked_ = true; dockedAt_ = now; undockedAt_ = kNever; dockedName_ = station; dockedText_ = dockedText(station); }
    void onUndocked(double now) { if (docked_) { undockedAt_ = now; dockedAt_ = kNever; } docked_ = false; }
    bool docked() const { return docked_; }
    const std::string& dockedStatus() const { return docked_ ? dockedText_ : empty_; }
    // combat::Overheated / WeaponChanged
    void onOverheated(const std::string& name, double now) { overheatedAt_ = now; overheatedName_ = name; }
    void onWeaponChanged(const std::string& name, double now) { weaponChangedAt_ = now; weaponName_ = name; }
    void onEnemyKilled(double now) { killAt_ = now; }       // world::AsteroidDestroyed
    float killAge(double now) const { return killAt_ < -1e8 ? -1.0f : (float)(now - killAt_); }
    // gameplay::OreMined: repeated pickups of the same ore within kPickupMergeSeconds become one banner with the summed amount
    void onOreMined(const std::string& ore, const std::string& label, const RGB& colour, int amount, double now) {
        if (amount <= 0) return;
        Pickup* slot = nullptr;
        for (auto& p : pickups_) if (p.ore == ore && p.amount > 0 && now - p.at <= kPickupMergeSeconds && now - p.at < kPickupSeconds) { slot = &p; break; }
        if (slot) slot->amount += amount;
        else {
            slot = &pickups_[0];                                   // a free or expired slot, else the oldest
            for (auto& p : pickups_) { if (now - p.at >= kPickupSeconds || p.amount <= 0) { slot = &p; break; } if (p.at < slot->at) slot = &p; }
            slot->ore = ore; slot->label = label; slot->colour = colour; slot->amount = amount;
        }
        slot->at = now;
        slot->text = pickupText(slot->amount, slot->label);
    }
    // f(text, colour, alpha) for every live pickup banner, oldest slot order; alpha fades linearly over kPickupSeconds
    template <class F> void forEachPickup(double now, F&& f) const {
        for (auto& p : pickups_) {
            float a = timedAlpha(p.at, now, kPickupSeconds);
            if (p.amount > 0 && a > 0) f(p.text, p.colour, std::min(1.0f, a * 2.5f));
        }
    }
    int pickupCount(double now) const { int n = 0; forEachPickup(now, [&](const std::string&, const RGB&, float) { n++; }); return n; }
    // gameplay::CargoFull: at most one banner per kCargoFullMinGap seconds
    void onCargoFull(double now) { if (now - cargoFullAt_ >= kCargoFullMinGap) cargoFullAt_ = now; }
    void onRespawned() { impactAt_ = shieldBrokenAt_ = fuelEmptyAt_ = kNever; }   // the orbit lock announces its own release

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
        if (float a = timedAlpha(orbitReleasedAt_, now, kOrbitReleasedSeconds); a > 0) out.push_back({Warn::OrbitReleased, "ORBIT RELEASED", std::min(1.0f, a * 3)});
        if (float a = timedAlpha(dockedAt_, now, kDockBannerSeconds); a > 0) out.push_back({Warn::Docked, "DOCKED", std::min(1.0f, a * 3)});
        if (float a = timedAlpha(undockedAt_, now, kDockBannerSeconds); a > 0) out.push_back({Warn::Undocked, "UNDOCKED", std::min(1.0f, a * 3)});
        if (float a = timedAlpha(overheatedAt_, now, kEventBannerSeconds - 0.5f); a > 0) out.push_back({Warn::WeaponOverheated, upper(overheatedName_) + " OVERHEATED", flash * std::min(1.0f, a * 3)});
        if (float a = timedAlpha(weaponChangedAt_, now, kWeaponBannerSeconds); a > 0) out.push_back({Warn::WeaponChanged, "WEAPON: " + upper(weaponName_), std::min(1.0f, a * 3)});
        if (float a = timedAlpha(cargoFullAt_, now, kCargoFullSeconds); a > 0) out.push_back({Warn::CargoFull, "CARGO FULL", flash * std::min(1.0f, a * 3)});
        return out;
    }

private:
    static constexpr double kNever = -1e9;
    static float timedAlpha(double at, double now, float dur) {
        double age = now - at;
        return (age < 0 || age >= dur) ? 0.0f : (float)(1.0 - age / dur);
    }
    double impactAt_ = kNever, shieldBrokenAt_ = kNever, fuelEmptyAt_ = kNever, orbitReleasedAt_ = kNever, dockedAt_ = kNever, undockedAt_ = kNever, overheatedAt_ = kNever, weaponChangedAt_ = kNever, killAt_ = kNever, cargoFullAt_ = kNever;
    Pickup pickups_[kMaxPickups];
    std::string overheatedName_, weaponName_;
    bool docked_ = false;
    std::string orbitText_, dockedText_, empty_;
    std::string dockedName_;
    bool orbitLocked_ = false;
    std::string orbitBody_;
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

// "RESPAWNING IN 3": N = ceil(secondsLeft); empty when nothing is left to count.
inline std::string respawnText(float secondsLeft) {
    if (secondsLeft <= 0) return "";
    return "RESPAWNING IN " + std::to_string((int)std::ceil(secondsLeft));
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
