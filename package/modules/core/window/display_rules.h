#pragma once
// Pure display logic (no SDL types, no GL): the display list model, aspect classes, which display to use, which video mode / resolution, where to
// place the window, the resolution list for the settings page, and the "retro" hint. core/window fills a DisplayInfo list from SDL (or from
// --fake-displays) and asks these functions; everything is unit-tested with fake data in tests/test_display.cpp. See docs/DISPLAYS.md.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace core::display {

struct Mode { int w = 0, h = 0, hz = 0; };
inline bool operator==(const Mode& a, const Mode& b) { return a.w == b.w && a.h == b.h && a.hz == b.hz; }

struct DisplayInfo {
    int index = 0;                 // SDL display index (0-based)
    std::string name;
    int x = 0, y = 0, w = 0, h = 0;   // bounds in the desktop coordinate space (the desktop mode's size)
    int refreshHz = 0;             // desktop mode refresh, 0 if unknown
    float dpiX = 0, dpiY = 0;      // 0 = unknown
    std::vector<Mode> modes;       // every mode the display offers (SDL_GetDisplayMode)
    bool fake = false;             // came from --fake-displays
    float widthMm() const { return dpiX > 0 ? w / dpiX * 25.4f : 0; }
    float heightMm() const { return dpiY > 0 ? h / dpiY * 25.4f : 0; }
};

// ---- aspect ----
enum class Aspect { R4_3, R5_4, R16_10, R16_9, UltraWide, Other };
inline Aspect classifyAspect(int w, int h) {
    if (w <= 0 || h <= 0) return Aspect::Other;
    double r = (double)w / h;
    struct C { double ratio; Aspect a; };
    static const C table[] = {{5.0 / 4, Aspect::R5_4}, {4.0 / 3, Aspect::R4_3}, {16.0 / 10, Aspect::R16_10}, {16.0 / 9, Aspect::R16_9}};
    for (const C& c : table) if (std::fabs(r - c.ratio) < 0.025) return c.a;     // 1366x768 (1.779) is 16:9, 1360x768 too
    if (r >= 2.1) return Aspect::UltraWide;                                     // 21:9 (2.33), 2.39, 32:9
    return Aspect::Other;
}
inline const char* aspectName(Aspect a) {
    switch (a) {
        case Aspect::R4_3: return "4:3";
        case Aspect::R5_4: return "5:4";
        case Aspect::R16_10: return "16:10";
        case Aspect::R16_9: return "16:9";
        case Aspect::UltraWide: return "ultrawide";
        default: return "other";
    }
}

// Purely informational (logs, hints): a display that looks like a CRT: 4:3 or 5:4, at least 60 Hz, at most 1280 wide.
inline bool isRetro(const DisplayInfo& d) {
    Aspect a = classifyAspect(d.w, d.h);
    return (a == Aspect::R4_3 || a == Aspect::R5_4) && d.refreshHz >= 60 && d.w <= 1280;
}

// ---- which display ----
inline int displayContaining(const std::vector<DisplayInfo>& d, int px, int py) {
    for (const DisplayInfo& x : d) if (px >= x.x && px < x.x + x.w && py >= x.y && py < x.y + x.h) return x.index;
    return -1;
}

struct Choice {
    int index = 0;              // position in the list (== the SDL index)
    std::string why;            // one sentence for the startup log
    bool fellBack = false;      // a requested display did not exist
};
// setting: "auto" / "" or a number ("video.display"); flagIndex: --display=N (-1 = not given, wins over the setting).
// auto = the display the mouse is on (when known), else the primary (index 0).
inline Choice chooseDisplay(const std::vector<DisplayInfo>& d, const std::string& setting, int flagIndex, bool haveMouse, int mouseX, int mouseY) {
    Choice c;
    if (d.empty()) { c.why = "no displays reported: using display 0"; return c; }
    int want = -1;
    std::string source;
    if (flagIndex >= 0) { want = flagIndex; source = "--display flag"; }
    else if (!setting.empty() && setting != "auto") {
        char* end = nullptr;
        long v = std::strtol(setting.c_str(), &end, 10);
        if (end && *end == '\0' && v >= 0) { want = (int)v; source = "setting video.display"; }
        else { c.fellBack = true; c.why = "video.display '" + setting + "' is not 'auto' or a number: "; }
    }
    if (want >= 0) {
        if (want < (int)d.size()) { c.index = want; c.why = "chosen by " + source; return c; }
        c.fellBack = true;
        c.why = "display " + std::to_string(want) + " (" + source + ") does not exist, " + std::to_string(d.size()) + " found: ";
    }
    if (haveMouse) {
        int m = displayContaining(d, mouseX, mouseY);
        if (m >= 0) { c.index = m; c.why += "auto: the display the mouse is on"; return c; }
    }
    c.index = 0;
    c.why += "auto: the primary display";
    return c;
}

// ---- video mode and resolution ----
enum class VideoMode { Borderless, Exclusive, Windowed };
inline const char* videoModeName(VideoMode m) { return m == VideoMode::Borderless ? "borderless" : m == VideoMode::Exclusive ? "exclusive" : "windowed"; }
inline bool parseVideoMode(const std::string& s, VideoMode& out) {
    if (s == "borderless") { out = VideoMode::Borderless; return true; }
    if (s == "exclusive") { out = VideoMode::Exclusive; return true; }
    if (s == "windowed") { out = VideoMode::Windowed; return true; }
    return false;
}

struct Res { int w = 0, h = 0, hz = 0; };   // hz 0 = any
// "1024x768", "1024x768@85"
inline bool parseResolution(const std::string& s, Res& out) {
    int w = 0, h = 0, hz = 0;
    char x = 0, at = 0;
    int n = std::sscanf(s.c_str(), "%d%c%d%c%d", &w, &x, &h, &at, &hz);
    if (n < 3 || (x != 'x' && x != 'X') || (n >= 4 && at != '@')) return false;
    if (n == 4) return false;                                                     // "1024x768@" with no number
    if (w < 160 || h < 120 || w > 16384 || h > 16384 || hz < 0 || hz > 1000) return false;
    out = {w, h, n == 5 ? hz : 0};
    return true;
}

// The available mode closest to `want`: smallest squared distance in width/height first, then the refresh closest to want.hz (hz 0 = the highest).
// Returns false only when the list is empty. `exact` says whether it matched.
inline bool closestMode(const std::vector<Mode>& modes, const Res& want, Mode& out, bool* exact = nullptr) {
    if (modes.empty()) return false;
    const Mode* best = nullptr;
    double bestDist = 0, bestHz = 0;
    for (const Mode& m : modes) {
        double dw = m.w - want.w, dh = m.h - want.h, dist = dw * dw + dh * dh;
        double hz = want.hz > 0 ? std::fabs((double)m.hz - want.hz) : -(double)m.hz;
        if (!best || dist < bestDist || (dist == bestDist && hz < bestHz)) { best = &m; bestDist = dist; bestHz = hz; }
    }
    out = *best;
    if (exact) *exact = best->w == want.w && best->h == want.h && (want.hz == 0 || best->hz == want.hz);
    return true;
}

// The list for the settings page: one entry per (w,h) with its highest refresh, nothing below minW x minH, biggest first.
inline std::vector<Res> selectableResolutions(const std::vector<Mode>& modes, int minW = 640, int minH = 480) {
    std::vector<Res> out;
    for (const Mode& m : modes) {
        if (m.w < minW || m.h < minH) continue;
        auto it = std::find_if(out.begin(), out.end(), [&](const Res& r) { return r.w == m.w && r.h == m.h; });
        if (it == out.end()) out.push_back({m.w, m.h, m.hz});
        else it->hz = std::max(it->hz, m.hz);
    }
    std::sort(out.begin(), out.end(), [](const Res& a, const Res& b) { return a.w * (long)a.h != b.w * (long)b.h ? a.w * (long)a.h > b.w * (long)b.h : a.w > b.w; });
    return out;
}
inline std::string resolutionText(const Res& r) {
    std::string s = std::to_string(r.w) + "x" + std::to_string(r.h);
    if (r.hz > 0) s += "@" + std::to_string(r.hz);
    return s;
}

// ---- window placement ----
struct Placement { int x = 0, y = 0; };
// The window's top-left when it is centred ON display d (desktop coordinates: a display can have a non-zero origin). A window bigger than the
// display goes to the display's origin rather than half off it.
inline Placement centerOn(const DisplayInfo& d, int winW, int winH) {
    return {d.x + std::max(0, (d.w - winW) / 2), d.y + std::max(0, (d.h - winH) / 2)};
}
// A windowed size that fits: never bigger than the display (keeps a small margin for decorations unless it is already smaller).
inline Res fitWindow(const DisplayInfo& d, Res want) {
    if (d.w <= 0 || d.h <= 0) return want;
    want.w = std::min(want.w, d.w);
    want.h = std::min(want.h, d.h);
    return want;
}

// ---- --fake-displays=1366x768;1280x1024 (test hook) ----
// Side by side, left to right, the first one at 0,0. Each offers its native mode plus the usual smaller ones.
inline std::vector<DisplayInfo> parseFakeDisplays(const std::string& spec) {
    std::vector<DisplayInfo> out;
    size_t pos = 0;
    int x = 0;
    while (pos <= spec.size() && !spec.empty()) {
        size_t semi = spec.find(';', pos);
        std::string item = spec.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        Res r;
        if (parseResolution(item, r)) {
            DisplayInfo d;
            d.index = (int)out.size();
            d.name = "FAKE-" + std::to_string(d.index + 1);
            d.x = x; d.y = 0; d.w = r.w; d.h = r.h; d.refreshHz = r.hz > 0 ? r.hz : 60;
            d.dpiX = d.dpiY = 96.0f;
            d.fake = true;
            static const Mode std[] = {{640, 480, 0}, {800, 600, 0}, {1024, 768, 0}, {1280, 720, 0}, {1280, 1024, 0}, {1366, 768, 0}, {1920, 1080, 0}};
            for (Mode m : std) if (m.w <= r.w && m.h <= r.h && !(m.w == r.w && m.h == r.h)) { m.hz = d.refreshHz; d.modes.push_back(m); }
            d.modes.push_back({r.w, r.h, d.refreshHz});
            x += r.w;
            out.push_back(d);
        }
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
    return out;
}

// ---- text (startup log block, --list-displays) ----
inline std::string describeDisplay(const DisplayInfo& d, bool chosen = false) {
    char b[400];
    std::string size;
    if (d.dpiX > 0) { char m[64]; std::snprintf(m, sizeof m, "  %.0fx%.0f mm (%.0f dpi)", d.widthMm(), d.heightMm(), d.dpiX); size = m; }
    std::snprintf(b, sizeof b, "display %d: %s  %dx%d @ %d Hz  at (%d,%d)%s  %s  %zu modes%s%s%s", d.index, d.name.c_str(), d.w, d.h, d.refreshHz, d.x, d.y, size.c_str(),
                  aspectName(classifyAspect(d.w, d.h)), d.modes.size(), isRetro(d) ? "  [retro/CRT-like]" : "", d.fake ? "  [FAKE]" : "", chosen ? "  <== chosen" : "");
    return b;
}

} // namespace core::display
