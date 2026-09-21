#pragma once
// Pure Ore Scanner presentation rules (the "ore_scanner" perk, docs/MINING.md "Ore Scanner"): no GL, no engine types.
// Shared by combat/weapons (lock bracket tint), ui/ship_hud (the lock info line) and ship/cockpit (radar dots). Unit-tested in package/tests/test_scanner.cpp.
// Without the perk the player only sees "ASTEROID", its radius and distance; with it the ore type, its colour and the expected yield.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace combat {

constexpr const char* kScannerPerk = "ore_scanner";
constexpr int kRareOreRarity = 8;                       // data/ores.json rarity <= this = a valuable ore (gold, uranium, platinum, crystal)

// Expected ore units from a rock: the SAME formula as gameplay::totalYield (gameplay/mining/mining_rules.h, tunable mining.yield_scale).
// Recomputed here so combat/ui do not depend on the mining module; keep the two in step.
inline int expectedYield(float radius, float yieldScale) { return std::max(1, (int)std::lround((double)yieldScale * radius * radius)); }

// Upper-case display name: the ores.json "name" when set, else the id.
inline std::string oreDisplayName(const std::string& id, const std::string& name) {
    std::string s = name.empty() ? id : name;
    for (char& ch : s) ch = (char)std::toupper((unsigned char)ch);
    return s;
}

// "ASTEROID  r 9.6  850 m" without the scanner, "ASTEROID  IRON  r 9.6  ~41 ore  850 m" with it (an unknown ore name is left out).
inline std::string lockInfoText(bool scanner, const std::string& oreName, float radius, float distance, float yieldScale) {
    char b[128];
    int d = (int)std::lround(std::max(0.0f, distance));
    if (!scanner) std::snprintf(b, sizeof b, "ASTEROID  r %.1f  %d m", radius, d);
    else if (oreName.empty()) std::snprintf(b, sizeof b, "ASTEROID  r %.1f  ~%d ore  %d m", radius, expectedYield(radius, yieldScale), d);
    else std::snprintf(b, sizeof b, "ASTEROID  %s  r %.1f  ~%d ore  %d m", oreName.c_str(), radius, expectedYield(radius, yieldScale), d);
    return b;
}

// The distance bucket the HUD rebuilds its cached line on (10 m steps: the text changes a few times a second at most).
inline int lockDistanceBucket(float distance) { return (int)std::lround(std::max(0.0f, distance) / 10.0f); }

struct OreLook { float r = 0.5f, g = 0.45f, b = 0.35f; int tierBonus = 0; };
constexpr OreLook kUnscannedLook{};                     // the dim tan dot / default look

// Look of an ore on a display: its data colour brightened so the brightest channel is at least `minBright` (rock tints are dark),
// plus one size tier for rare ores. No scanner, or an unknown ore (no colour), gives the unscanned look.
inline OreLook oreLook(bool scanner, bool known, const float rgb[3], int rarity, float minBright = 0.75f) {
    if (!scanner || !known || !rgb) return kUnscannedLook;
    float m = std::max({rgb[0], rgb[1], rgb[2]});
    if (!(m > 0)) return kUnscannedLook;
    float k = m < minBright ? minBright / m : 1.0f;
    OreLook o;
    o.r = std::min(1.0f, rgb[0] * k); o.g = std::min(1.0f, rgb[1] * k); o.b = std::min(1.0f, rgb[2] * k);
    o.tierBonus = rarity <= kRareOreRarity ? 1 : 0;
    return o;
}

} // namespace combat
