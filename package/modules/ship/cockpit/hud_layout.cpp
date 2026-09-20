#include "ship/cockpit/hud_layout.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace cockpit {

namespace {
std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) {
    char buf[96];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}
} // namespace

Rgb levelColor(Level l) {
    switch (l) {
        case Level::Ok:   return {0.25f, 0.95f, 0.45f};
        case Level::Warn: return {1.00f, 0.65f, 0.15f};
        case Level::Bad:  return {1.00f, 0.22f, 0.15f};
        case Level::Dim:  return {0.40f, 0.45f, 0.48f};
    }
    return {1, 1, 1};
}

Level levelForFraction(float frac) { return frac > 0.5f ? Level::Ok : frac > 0.25f ? Level::Warn : Level::Bad; }
Rgb barColor(float frac) { return levelColor(levelForFraction(frac)); }

float fraction(float value, float max) { return max > 0 ? std::clamp(value / max, 0.0f, 1.0f) : 0.0f; }

std::vector<StatusLine> systemsLines(const ship::ShipStatus& s) {
    std::vector<StatusLine> out;
    out.push_back({fmt("HULL %.0f/%.0f", s.hp, s.maxHp), levelForFraction(fraction(s.hp, s.maxHp))});
    if (!s.shieldInstalled)   out.push_back({"SHIELD NOT FITTED", Level::Dim});
    else if (!s.shieldEnabled) out.push_back({"SHIELD DISABLED", Level::Warn});
    else if (s.shield <= 0)    out.push_back({"SHIELD DOWN", Level::Bad});
    else                       out.push_back({fmt("SHIELD %.0f/%.0f", s.shield, s.maxShield), levelForFraction(fraction(s.shield, s.maxShield))});
    out.push_back({fmt("WARP FUEL %.0f/%.0f", s.warpFuel, s.maxWarpFuel), levelForFraction(fraction(s.warpFuel, s.maxWarpFuel))});
    out.push_back(s.warping ? StatusLine{"DRIVE ENGAGED", Level::Warn} : StatusLine{"DRIVE STANDBY", Level::Ok});
    out.push_back(s.alive ? StatusLine{"STATUS ALIVE", Level::Ok} : StatusLine{"STATUS DESTROYED", Level::Bad});
    return out;
}

std::string speedText(float mps) { return fmt("%.0f", std::max(mps, 0.0f)); }

std::string headingText(const engine::Vec3& f) {
    float heading = std::atan2(f.x, -f.z + 0.0f) * 57.29578f;   // "+ 0.0f": -0.0 would flip straight up/down to 180
    if (heading < 0) heading += 360.0f;
    if (heading >= 359.5f) heading = 0;                       // rounds to 360 otherwise
    float pitch = std::asin(std::clamp(f.y, -1.0f, 1.0f)) * 57.29578f;
    return fmt("HDG %03.0f  PIT %+.0f", heading, pitch);
}

std::string percentText(float frac) { return fmt("%.0f%%", std::clamp(frac, 0.0f, 1.0f) * 100.0f); }

} // namespace cockpit
