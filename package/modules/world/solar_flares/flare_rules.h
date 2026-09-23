#pragma once
// Pure solar flare rules: no GL, no SDL, no engine services. Unit-tested in package/tests/test_solar_flares.cpp. Design: docs/SOLAR_FLARES.md.
// Seeded interval roll, warning countdown, shell radius over time, the swept "did the front pass the ship this step" test, damage falloff with
// distance from the sun (the missile blastDamage shape: max near the sun, linear to 0 at max_range), and the whole cycle as one step function.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace world {

struct FlareParams {
    double intervalMin = 90, intervalMax = 240;   // seconds between the end of one flare and the next eruption (rolled per cycle)
    double warningSeconds = 15;                   // SolarFlareWarning is emitted this long before the eruption
    double shellSpeed = 4000;                     // units/s the front grows outward
    double shellThickness = 1500;                 // units: the front's width (the ship is hit while inside it)
    double maxRange = 900000;                     // units from the sun's centre: the flare ends once the front passes this
    double startRadius = 1000;                    // units: the front starts at the sun's surface (the sun's radius)
    float damageAtSun = 120;                      // damage at (or inside) damageMinRange
    double damageMinRange = 5000;                 // units from the sun's centre: always full damage inside this
    double damageFalloffRange = 30000;            // units from the sun's centre: damage reaches 0 here (independent of maxRange, which is just
                                                   // where the front stops existing - maxRange is ~3x the outer orbit, far past where damage should
                                                   // still matter, so a separate, much shorter falloff keeps outer planets/most of the system safe)
};

// Clamps nonsense tunables into something that runs (min <= max, positive speeds, ...). NaN falls back to the defaults.
inline FlareParams sanitizeFlareParams(FlareParams p) {
    const FlareParams d;
    auto fin = [](double v, double def) { return std::isfinite(v) ? v : def; };
    p.intervalMin = std::max(1.0, fin(p.intervalMin, d.intervalMin));
    p.intervalMax = std::max(p.intervalMin, fin(p.intervalMax, d.intervalMax));
    p.warningSeconds = std::max(0.0, fin(p.warningSeconds, d.warningSeconds));
    p.shellSpeed = std::max(1.0, fin(p.shellSpeed, d.shellSpeed));
    p.shellThickness = std::max(1.0, fin(p.shellThickness, d.shellThickness));
    p.startRadius = std::max(0.0, fin(p.startRadius, d.startRadius));
    p.maxRange = std::max(p.startRadius + 1.0, fin(p.maxRange, d.maxRange));
    p.damageAtSun = std::isfinite(p.damageAtSun) ? std::max(0.0f, p.damageAtSun) : d.damageAtSun;
    p.damageMinRange = std::max(0.0, fin(p.damageMinRange, d.damageMinRange));
    p.damageFalloffRange = std::max(p.damageMinRange + 1.0, fin(p.damageFalloffRange, d.damageFalloffRange));
    return p;
}

// ---- seeded interval ----
// splitmix-style hash: same (seed, cycle) -> same value, no global state.
inline uint32_t flareHash(uint32_t seed, uint32_t cycle) {
    uint64_t z = ((uint64_t)seed << 32 | cycle) + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (uint32_t)((z ^ (z >> 31)) >> 32);
}
// Seconds until the eruption of flare number `cycle`, uniform in [intervalMin, intervalMax].
inline double flareInterval(uint32_t seed, uint32_t cycle, double intervalMin, double intervalMax) {
    double u = (double)flareHash(seed, cycle) / 4294967296.0;   // [0, 1)
    if (intervalMax < intervalMin) std::swap(intervalMin, intervalMax);
    return intervalMin + (intervalMax - intervalMin) * u;
}

// ---- warning ----
// The countdown to show, or -1 when the eruption is further off than the warning window (or already past).
inline double flareWarningEta(double timeUntilEruption, double warningSeconds) {
    if (!(timeUntilEruption > 0) || timeUntilEruption > warningSeconds) return -1;
    return timeUntilEruption;
}

// ---- the expanding front ----
inline double flareShellRadius(double sinceEruption, double shellSpeed, double startRadius) {
    return startRadius + std::max(0.0, sinceEruption) * std::max(0.0, shellSpeed);
}
// Did the front pass through a point `dist` from the sun's centre while it grew from prevRadius to radius (one step)? Swept, so a fast front
// can never jump over the ship between two steps. The front is `thickness` wide, centred on its radius.
inline bool flareShellHits(double dist, double prevRadius, double radius, double thickness) {
    if (!(dist >= 0)) return false;
    const double h = std::max(0.0, thickness) * 0.5;
    return dist >= std::min(prevRadius, radius) - h && dist <= std::max(prevRadius, radius) + h;
}

// ---- damage ----
// Full damage inside minRange, then linear to 0 at damageFalloffRange (the missile blastDamage shape; falloffRange is independent of maxRange,
// which just governs how far the front physically travels before the flare ends). Distances are from the sun's centre.
inline float flareDamage(double dist, const FlareParams& p) {
    if (!(dist >= 0) || p.damageAtSun <= 0) return 0.0f;
    if (dist <= p.damageMinRange) return p.damageAtSun;
    const double span = p.damageFalloffRange - p.damageMinRange;
    if (span <= 0) return 0.0f;
    const double k = 1.0 - (dist - p.damageMinRange) / span;
    return k <= 0 ? 0.0f : (float)(p.damageAtSun * k);
}

// ---- the whole cycle ----
// Waiting (counting down to the next eruption) -> Erupting (front growing) -> back to Waiting with a freshly rolled interval once the front
// passes maxRange. The next countdown starts when a flare ENDS, so two fronts never overlap.
struct FlareState {
    uint32_t seed = 1234;
    uint32_t cycle = 0;              // which flare is next / current (the interval roll's input)
    bool erupting = false;
    double untilEruption = 0;        // seconds (Waiting)
    double sinceEruption = 0;        // seconds (Erupting)
    double radius = -1;              // current front radius, -1 while Waiting
};
inline FlareState startFlareCycle(uint32_t seed, const FlareParams& p, uint32_t cycle = 0) {
    FlareState s;
    s.seed = seed; s.cycle = cycle;
    s.untilEruption = flareInterval(seed, cycle, p.intervalMin, p.intervalMax);
    return s;
}

struct FlareStep {
    double warningEta = -1;          // >= 0: emit SolarFlareWarning{warningEta} this step
    bool erupted = false;            // emit SolarFlareErupted this step
    bool ended = false;              // emit SolarFlareEnded this step
    double prevRadius = -1, radius = -1;   // the front's sweep this step (both >= 0 only while erupting): feed flareShellHits
};

inline FlareStep stepFlare(FlareState& s, const FlareParams& p, double dt) {
    FlareStep out;
    if (!(dt > 0)) dt = 0;
    if (!s.erupting) {
        s.untilEruption -= dt;
        if (s.untilEruption > 0) {
            out.warningEta = flareWarningEta(s.untilEruption, p.warningSeconds);
            return out;
        }
        // erupt: the overshoot past zero already counts as time since eruption
        s.erupting = true;
        s.sinceEruption = std::max(0.0, -s.untilEruption);
        s.untilEruption = 0;
        out.erupted = true;
        out.prevRadius = p.startRadius;
    } else {
        out.prevRadius = s.radius;
        s.sinceEruption += dt;
    }
    s.radius = flareShellRadius(s.sinceEruption, p.shellSpeed, p.startRadius);
    out.radius = s.radius;
    if (s.radius - p.shellThickness * 0.5 > p.maxRange) {   // the whole front is past max_range: done, roll the next one
        out.ended = true;
        s.erupting = false;
        s.radius = -1;
        s.sinceEruption = 0;
        s.cycle++;
        s.untilEruption = flareInterval(s.seed, s.cycle, p.intervalMin, p.intervalMax);
    }
    return out;
}

// Outermost planet orbit x 3 (the spec's suggested max_range), with a floor so a system without planets still has a flare.
inline double autoFlareMaxRange(double outermostOrbit, double sunRadius) {
    return std::max(outermostOrbit * 3.0, std::max(0.0, sunRadius) * 20.0 + 10000.0);
}

} // namespace world
