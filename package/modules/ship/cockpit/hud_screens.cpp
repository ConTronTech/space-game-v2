#include "ship/cockpit/hud_screens.h"
#include <cmath>
#include "engine/log.h"
#include "ship/cockpit/hud_layout.h"

namespace cockpit {

namespace {
const ScreenColor kBorder{0.0f, 0.45f, 0.6f, 1};
const ScreenColor kTitle{0.35f, 0.85f, 0.95f, 1};
ScreenColor col(Rgb c) { return {c.r, c.g, c.b, 1}; }
} // namespace

std::string hudContentFor(const std::string& content, const std::string& quadName) {
    if (!content.empty()) return content;
    if (quadName == "INFO") return "FLIGHT_DATA";
    if (quadName == "SYSTEMS") return "SHIP_SYSTEMS";
    if (quadName == "RADAR") return "PROXIMITY_RADAR";
    return "";
}

void HudScreens::draw(const ScreenContext& ctx) {
    ScreenCanvas& c = ctx.canvas;
    c.frame(0.01f, 0.01f, c.aspect() - 0.02f, 0.98f, 0.018f, kBorder);

    std::string what = hudContentFor(ctx.content, ctx.quad.name);
    if (what == "PROXIMITY_RADAR") { proximityRadar(ctx); return; }   // the scope is drawn without ship data: contacts come later

    const ship::IShip* ship = eng_.services.get<ship::IShip>();
    if (!ship) {
        if (!warnedNoShip_) { warnedNoShip_ = true; LOG_W("cockpit", "no ship::IShip: the HUD screens show NO DATA"); }
        noData(ctx);
        return;
    }
    warnedNoShip_ = false;      // it came back (or a new ship module loaded): warn again if it vanishes
    if (what == "FLIGHT_DATA") flightData(ctx, *ship);
    else if (what == "SHIP_SYSTEMS") shipSystems(ctx, *ship);
    else c.textCentered(c.aspect() * 0.5f, 0.42f, "NO SIGNAL", 0.14f, {0.5f, 0.55f, 0.6f, 1});
}

void HudScreens::noData(const ScreenContext& ctx) {
    ScreenCanvas& c = ctx.canvas;
    c.textCentered(c.aspect() * 0.5f, 0.38f, "NO DATA", 0.20f, col(levelColor(Level::Bad)));
}

void HudScreens::flightData(const ScreenContext& ctx, const ship::IShip& ship) {
    ScreenCanvas& c = ctx.canvas;
    const ship::ShipStatus& s = ship.status();
    const float x0 = 0.07f;

    c.text(x0, 0.06f, "SPD", 0.08f, kTitle);
    c.text(x0 + 0.21f, 0.03f, speedText(s.speed), 0.16f, {0.3f, 1.0f, 0.75f, 1});
    c.text(x0 + 0.21f + textWidth(speedText(s.speed), 0.16f) + 0.03f, 0.10f, "M/S", 0.07f, kTitle);
    c.text(x0, 0.24f, headingText(ship.forward()), 0.075f, {0.6f, 0.75f, 0.95f, 1});

    struct Row { const char* label; float value, max; bool fitted; };
    const Row rows[3] = {
        {"HP", s.hp, s.maxHp, true},
        {"SHD", s.shield, s.maxShield, s.shieldInstalled && s.shieldEnabled},
        {"FUEL", s.warpFuel, s.maxWarpFuel, true},
    };
    float y = 0.40f;
    for (const Row& r : rows) {
        float f = fraction(r.value, r.max);
        ScreenColor tint = r.fitted ? col(barColor(f)) : col(levelColor(Level::Dim));
        c.text(x0, y + 0.005f, r.label, 0.085f, tint);
        c.bar(x0 + 0.27f, y, 0.62f, 0.10f, r.fitted ? f : 0, tint);
        c.text(x0 + 0.27f + 0.62f + 0.04f, y + 0.005f, r.fitted ? percentText(f) : "OFF", 0.085f, tint);
        y += 0.19f;
    }
}

void HudScreens::shipSystems(const ScreenContext& ctx, const ship::IShip& ship) {
    ScreenCanvas& c = ctx.canvas;
    c.text(0.07f, 0.05f, "SYSTEMS", 0.075f, kTitle);
    c.line(0.07f, 0.16f, c.aspect() - 0.07f, 0.16f, 0.012f, kBorder);
    float y = 0.22f;
    for (const StatusLine& l : systemsLines(ship.status())) {
        c.text(0.07f, y, l.text, 0.085f, col(levelColor(l.level)));
        y += 0.145f;
    }
}

void HudScreens::proximityRadar(const ScreenContext& ctx) {
    ScreenCanvas& c = ctx.canvas;
    const float cx = c.aspect() * 0.5f, cy = 0.53f, r = 0.36f;
    const ScreenColor ring{0.0f, 0.5f, 0.35f, 1}, faint{0.0f, 0.3f, 0.22f, 1};
    for (int i = 1; i <= 3; i++) c.circle(cx, cy, r * (float)i / 3.0f, i == 3 ? 0.014f : 0.008f, i == 3 ? ring : faint, 40);
    c.line(cx - r, cy, cx + r, cy, 0.006f, faint);
    c.line(cx, cy - r, cx, cy + r, 0.006f, faint);

    // sweep: a bright arm with a fading tail behind it
    const float turn = 6.2831853f / 3.0f;                  // one revolution every 3 s
    float a = ctx.time * turn;
    for (int i = 0; i < 6; i++) {
        float ai = a - (float)i * 0.12f, fade = 1.0f - (float)i / 6.0f;
        c.line(cx, cy, cx + std::cos(ai) * r, cy + std::sin(ai) * r, i == 0 ? 0.012f : 0.009f, {0.1f, 0.9f * fade, 0.5f * fade, 1});
    }
    c.rect(cx - 0.015f, cy - 0.015f, 0.03f, 0.03f, {0.1f, 1.0f, 0.55f, 1});   // our ship, always at the centre

    c.text(0.06f, 0.05f, "RADAR", 0.07f, kTitle);
    c.textCentered(cx, cy + r * 0.45f, "NO CONTACTS", 0.065f, {0.0f, 0.75f, 0.5f, 1});
}

} // namespace cockpit
