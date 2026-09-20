#pragma once
// Pure text/number logic for the built-in HUD screens (what the lines say and how they are coloured), kept apart from the
// GL drawing so it can be unit-tested. Input is only ship::ShipStatus (the ship contract).
#include <string>
#include <vector>
#include "engine/math.h"
#include "ship/ship_core/ship_api.h"

namespace cockpit {

enum class Level { Ok, Warn, Bad, Dim };     // Dim = not fitted / off, drawn grey

struct Rgb { float r, g, b; };
Rgb levelColor(Level l);
Level levelForFraction(float frac);           // > 50% Ok, > 25% Warn, else Bad
Rgb barColor(float frac);                     // the same thresholds as a colour
float fraction(float value, float max);       // clamped 0..1, 0 when max <= 0

struct StatusLine { std::string text; Level level; };

// SHIP_SYSTEMS content: hull, shield, warp fuel, drive and life status as short lines (<= 20 characters each).
std::vector<StatusLine> systemsLines(const ship::ShipStatus& s);

std::string speedText(float metresPerSecond);            // "123"  (integer m/s, never negative)
std::string headingText(const engine::Vec3& forward);   // "HDG 045  PIT +10": compass heading (0 = -Z, 90 = +X) and pitch
std::string percentText(float frac);                     // "35%"

} // namespace cockpit
