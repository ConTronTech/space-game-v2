#pragma once
// Pure quality-preset logic (no SDL/GL/engine types): preset names, hardware detection heuristics, the table of tunables per preset.
// Unit-tested in tests/test_quality.cpp. See docs/QUALITY.md.
#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace quality {

enum class Preset { Auto, Low, Medium, High, Ultra };

inline const char* presetName(Preset p) {
    switch (p) {
        case Preset::Auto: return "auto";
        case Preset::Low: return "low";
        case Preset::Medium: return "medium";
        case Preset::High: return "high";
        case Preset::Ultra: return "ultra";
    }
    return "auto";
}
inline std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
inline std::string titleName(Preset p) { std::string s = presetName(p); s[0] = (char)std::toupper((unsigned char)s[0]); return s; }

struct ParsedPreset { Preset preset = Preset::Auto; bool unknown = false; };
inline ParsedPreset parsePreset(const std::string& text) {
    std::string t = lower(text);
    for (Preset p : {Preset::Auto, Preset::Low, Preset::Medium, Preset::High, Preset::Ultra}) if (t == presetName(p)) return {p, false};
    return {Preset::Auto, !t.empty()};      // empty = not set (auto, no complaint)
}
// Menu cycling: Auto -> Low -> Medium -> High -> Ultra -> Auto (dir = +1) and back (dir = -1).
inline Preset cyclePreset(Preset p, int dir) {
    int i = ((int)p + (dir >= 0 ? 1 : 4)) % 5;
    return (Preset)i;
}

// ---- hardware facts (from what core/window logs) ----
struct Hardware {
    std::string renderer, vendor, version;
    int maxTexture = 0;
    int cpuThreads = 0;        // 0 = unknown
    int ramMb = 0;             // 0 = unknown
    int displayW = 0, displayH = 0;
};

struct Detection { Preset preset = Preset::Medium; std::string reason; };

inline bool has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

// The first number after 'key' in 's' ("geforce gtx 1060" + "gtx " -> 1060), or -1.
inline int numberAfter(const std::string& s, const char* key) {
    auto p = s.find(key);
    if (p == std::string::npos) return -1;
    p += std::string(key).size();
    while (p < s.size() && s[p] == ' ') p++;
    if (p >= s.size() || !std::isdigit((unsigned char)s[p])) return -1;
    int n = 0;
    while (p < s.size() && std::isdigit((unsigned char)s[p])) n = n * 10 + (s[p++] - '0');
    return n;
}

// The GPU class decides; the CPU/RAM/texture limit/display can only lower the result. Every case names its reason (logged at startup).
//   software renderer (llvmpipe, softpipe, swrast, ...) ........ Low
//   Intel: Ironlake / Sandy Bridge / older, GMA, "Intel HD"..... Low  (the design target: an i5 M 560 with Ironlake graphics)
//          Iris / UHD / Xe .................................... Medium;  Arc ... High
//   NVIDIA: RTX ............................................... Ultra;  GTX / Quadro / other modern ... High;  old GeForce GT / 8xxx-9xxx ... Medium
//   AMD: Radeon RX 4-digit (5000+) ............................ Ultra;  Radeon RX ... High;  APU "Radeon Graphics"/Vega/HD/R5-R9 ... Medium
//   Apple ..................................................... High
//   unknown / empty ........................................... Medium
// Limits: max texture < 4096 -> Low; <= 2 CPU threads or < 3 GB RAM -> at most Medium; a 4K+ display -> Ultra becomes High.
inline Detection detect(const Hardware& hw) {
    std::string r = lower(hw.renderer), v = lower(hw.vendor);
    Detection d;
    if (r.empty() && v.empty()) return {Preset::Medium, "no renderer string: unknown hardware, assuming medium"};
    if (has(r, "llvmpipe") || has(r, "softpipe") || has(r, "swrast") || has(r, "software") || has(r, "gdi generic") || has(r, "microsoft basic render") || has(r, "swiftshader"))
        d = {Preset::Low, "software renderer"};
    else if (has(r, "intel") || (has(v, "intel") && !has(r, "nvidia") && !has(r, "radeon"))) {
        if (has(r, "arc")) d = {Preset::High, "Intel Arc"};
        else if (has(r, "iris") || has(r, "uhd") || has(r, " xe")) d = {Preset::Medium, "modern Intel integrated graphics"};
        else d = {Preset::Low, "older Intel integrated graphics (Ironlake/Sandy Bridge class)"};
    } else if (has(r, "nvidia") || has(r, "geforce") || has(r, "quadro") || has(r, "titan")) {
        int gt = numberAfter(r, "geforce gt ");
        int old = numberAfter(r, "geforce ");
        if (has(r, "rtx")) d = {Preset::Ultra, "NVIDIA RTX"};
        else if (has(r, "gtx") || has(r, "quadro") || has(r, "titan")) d = {Preset::High, "NVIDIA GTX/workstation"};
        else if (gt >= 0 || (old >= 0 && old < 1000)) d = {Preset::Medium, "older NVIDIA GeForce"};
        else d = {Preset::High, "NVIDIA"};
    } else if (has(r, "radeon") || has(r, "amd") || has(v, "amd") || has(v, "ati")) {
        int rx = numberAfter(r, "rx ");
        if (rx >= 5000) d = {Preset::Ultra, "AMD Radeon RX (recent)"};
        else if (rx >= 0 || has(r, "radeon pro") || has(r, "navi")) d = {Preset::High, "AMD Radeon RX"};
        else d = {Preset::Medium, "AMD integrated/older Radeon"};
    } else if (has(r, "apple")) d = {Preset::High, "Apple GPU"};
    else d = {Preset::Medium, "unrecognised renderer, assuming medium"};

    auto cap = [&](Preset limit, const std::string& why) {
        if ((int)d.preset > (int)limit) { d.preset = limit; d.reason += "; " + why; }
    };
    if (hw.maxTexture > 0 && hw.maxTexture < 4096) cap(Preset::Low, "max texture size only " + std::to_string(hw.maxTexture));
    if (hw.cpuThreads > 0 && hw.cpuThreads <= 2) cap(Preset::Medium, "only " + std::to_string(hw.cpuThreads) + " CPU threads");
    if (hw.ramMb > 0 && hw.ramMb < 3072) cap(Preset::Medium, "only " + std::to_string(hw.ramMb) + " MB RAM");
    if (hw.displayW >= 3840 && hw.displayH >= 2160) cap(Preset::High, "4K display");
    return d;
}

// ---- what a preset sets: DEFAULT values for existing tunables (game.json values always win) ----
struct Entry { const char* key; double low, medium, high, ultra; };

inline const std::vector<Entry>& presetTable() {
    static const std::vector<Entry> t = {
        // key                              low    medium  high    ultra     (medium = the built-in defaults)
        {"skybox.max_size",                 512,   1024,   2048,   2048},
        {"starfield.count",                 1200,  2500,   4000,   6000},
        {"world.sphere_detail",             0,     1,      1,      1},
        {"world.planet_max_lod",            2,     3,      3,      4},
        {"world.planet_triangle_budget",    8000,  20000,  40000,  80000},
        {"world.planet_lod_edge_px",        20,    12,     8,      6},
        {"asteroids.belt_asteroids",        600,   1500,   2500,   4000},
        {"asteroids.cluster_asteroids",     30,    60,     100,    150},
        {"asteroids.max_drawn",             150,   400,    700,    1200},
        {"asteroids.draw_distance",         3500,  6000,   8000,   10000},
        {"asteroids.triangle_budget",       6000,  15000,  30000,  60000},
        {"asteroids.lod_edge_px",           16,    10,     8,      6},
    };
    return t;
}

inline double valueFor(const Entry& e, Preset p) {
    switch (p) {
        case Preset::Low: return e.low;
        case Preset::High: return e.high;
        case Preset::Ultra: return e.ultra;
        default: return e.medium;
    }
}

} // namespace quality
