#pragma once
// Pure rules of the MAP tab (no GL, no SDL, no engine types): the logarithmic radial projection, pan/zoom clamping, marker size/colour, the belt band.
// Unit-tested in package/tests/test_system_map.cpp. See docs/SYSTEM_MAP.md.
//
// Scale: a point at distance d (units, in the XZ plane) from the view focus is drawn at a fraction
//     f(d) = log1p(d / knee) / log1p(range / knee),   knee = range / kLogKnee
// of the map's radius (clamped to 0..1). `range` is the zoom: the distance shown at the rim. With range 400,000 and the sun as focus the six
// seed-1234 planets land between 0.49 and 0.97 of the radius, evenly spread (a linear map would squeeze Planet 1 into the inner 12%).
// The same log1p idea as the cockpit radar (ship/cockpit/radar_map.h), written again here on purpose: the modules stay decoupled.
#include <algorithm>
#include <cmath>
#include <vector>

namespace sysmap {

constexpr double kLogKnee = 50.0;   // knee = range / 50: below the knee the map is nearly linear, above it logarithmic

inline double radialFraction(double d, double range) {
    if (!(range > 0.0) || !(d > 0.0)) return 0.0;
    double knee = range / kLogKnee;
    return std::clamp(std::log1p(d / knee) / std::log1p(kLogKnee), 0.0, 1.0);
}

struct P2 { float x = 0, y = 0; };

// World (x, z) -> screen, around the focus (fx, fz), map centre (cx, cy), map radius R pixels. +x is right, +z is DOWN on screen (top-down, north up = -z).
inline P2 project(double x, double z, double fx, double fz, double range, float cx, float cy, float R) {
    double dx = x - fx, dz = z - fz;
    double d = std::sqrt(dx * dx + dz * dz);
    if (d < 1e-9) return {cx, cy};
    double r = radialFraction(d, range) * R;
    return {cx + (float)(dx / d * r), cy + (float)(dz / d * r)};
}

// ---- pan / zoom state ----
struct View {
    double range = 400000;    // units shown at the rim
    double fx = 0, fz = 0;    // focus, world units (relative to the sun: the caller adds the sun position)
};

struct Limits {
    double zoomMin = 500, zoomMax = 1.0e6;   // range limits
    double panMax = 1.0e6;                   // the focus stays within this distance of the sun
};

// Sanitises the limits (positive, min <= max) then clamps the view. NaN/inf fall back to the defaults: never degenerate.
inline View clampView(View v, Limits L) {
    if (!(L.zoomMin > 1.0) || !std::isfinite(L.zoomMin)) L.zoomMin = 1.0;
    if (!(L.zoomMax >= L.zoomMin) || !std::isfinite(L.zoomMax)) L.zoomMax = std::max(L.zoomMin, 1.0e6);
    if (!(L.panMax >= 0.0) || !std::isfinite(L.panMax)) L.panMax = 0.0;
    if (!std::isfinite(v.range)) v.range = L.zoomMax;
    v.range = std::clamp(v.range, L.zoomMin, L.zoomMax);
    if (!std::isfinite(v.fx)) v.fx = 0;
    if (!std::isfinite(v.fz)) v.fz = 0;
    double d = std::sqrt(v.fx * v.fx + v.fz * v.fz);
    if (d > L.panMax && d > 0) { v.fx *= L.panMax / d; v.fz *= L.panMax / d; }
    return v;
}

constexpr double kZoomStep = 1.6;   // one +/- press
inline View zoomed(View v, int steps, Limits L) { v.range *= std::pow(kZoomStep, -steps); return clampView(v, L); }
// Pan by a fraction of the CURRENT range (so a press moves the same on screen at every zoom). dx right, dz down.
constexpr double kPanStep = 0.25;
inline View panned(View v, int dx, int dz, Limits L) { v.fx += dx * kPanStep * v.range; v.fz += dz * kPanStep * v.range; return clampView(v, L); }

// ---- markers ----
enum class Mark { Sun, Planet, Moon, Station, Ship };

// Marker half size in pixels, times the UI scale by the caller. Planets grow with the body radius (200 -> 4 px, 1,134 -> 7 px), moons stay small.
inline float markerSize(Mark m, double bodyRadius) {
    switch (m) {
        case Mark::Sun: return 8.0f;
        case Mark::Planet: return (float)std::clamp(3.0 + bodyRadius / 300.0, 3.0, 7.0);
        case Mark::Moon: return (float)std::clamp(1.5 + bodyRadius / 150.0, 1.5, 3.0);
        case Mark::Station: return 3.5f;
        case Mark::Ship: return 4.5f;
    }
    return 3.0f;
}

// A body's own colour, lifted so its brightest channel is at least `minPeak` (a dark planet must still read on the dark glass). Writes out[3].
inline void markerColor(const float in[3], float minPeak, float out[3]) {
    float peak = std::max({in[0], in[1], in[2], 1e-4f});
    float k = peak < minPeak ? minPeak / peak : 1.0f;
    for (int i = 0; i < 3; i++) out[i] = std::clamp(in[i] * k, 0.0f, 1.0f);
}

// A moon is drawn at least this many pixels from its planet's marker (its true offset collapses to zero at system zoom): a schematic spread,
// moon `index` (0, 1, 2 around that planet) at planetSize + 5 + 4 * index pixels, in the moon's real direction.
inline float moonMinOffset(float planetSize, int index) { return planetSize + 5.0f + 4.0f * (float)index; }

// ---- asteroid belt band ----
struct Band { double inner = 0, outer = 0; bool valid = false; };

// The belt as a radial band around the sun, from the asteroids' sun distances (world/asteroids does not expose belt geometry, only positions).
// The belt (1,500 rocks) outnumbers the planet clusters (4 x 60), so the median distance is inside it; the band is the min/max of the rocks
// within `halfWidth` of that median (the generator caps a belt at 3,000 wide). Fewer than 20 rocks = no band. Sorts `r` in place.
inline Band beltBand(std::vector<double>& r, double halfWidth = 5000.0) {
    Band b;
    if (r.size() < 20) return b;
    std::sort(r.begin(), r.end());
    double med = r[r.size() / 2];
    b.inner = med; b.outer = med;
    for (double v : r) if (std::fabs(v - med) <= halfWidth) { b.inner = std::min(b.inner, v); b.outer = std::max(b.outer, v); }
    b.valid = b.outer > b.inner;
    return b;
}

} // namespace sysmap
