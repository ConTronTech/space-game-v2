#include "ship/cockpit/hud_screens.h"
#include <cmath>
#include "core/camera/camera_api.h"
#include "engine/log.h"
#include "core/data_registry/data_api.h"
#include "gameplay/inventory/inventory_api.h"
#include "ship/docking/docking_api.h"
#include "world/asteroids/asteroids_api.h"
#include "world/star_system/star_system_api.h"
#include "world/stations/stations_api.h"
#include "ship/cockpit/hud_layout.h"
#include "ship/cockpit/radar_map.h"

namespace cockpit {

namespace {
const ScreenColor kBorder{0.0f, 0.45f, 0.6f, 1};
const ScreenColor kTitle{0.35f, 0.85f, 0.95f, 1};
ScreenColor col(Rgb c) { return {c.r, c.g, c.b, 1}; }
} // namespace

HudScreens::HudScreens(engine::Engine& eng) : eng_(eng) {
    asteroidRange_ = std::max(100.0f, eng.config.get("cockpit.radar_asteroid_range", kDefaultAsteroidRange, "radar: asteroids closer than this many units are shown as tiny dots (the 12 nearest)"));
    rockRate_.setHz(4.0f);
    range_ = std::max(1000.0f, eng.config.get("cockpit.radar_range", 400000.0f, "radar rim distance in units (logarithmic scale: near bodies stay readable, far ones clamp to the rim)"));
}

RockDot HudScreens::rockDot(const std::string& ore, int sizeTier) {
    auto it = ores_.find(ore);
    if (it == ores_.end()) {                                // data/ores.json colour + rarity, looked up once per ore id
        OreDot o;
        if (const core::IData* data = eng_.services.get<core::IData>()) {
            const engine::Json& j = data->get("ores", ore);
            if (j["color"].size() >= 3) { o.known = true; for (int k = 0; k < 3; k++) o.rgb[k] = (float)j["color"].at((size_t)k).num(0.5); }
            o.rarity = (int)j["rarity"].num(100);
        }
        it = ores_.emplace(ore, o).first;
    }
    return radarRockDot(sizeTier, true, it->second.known, it->second.rgb, it->second.rarity);
}

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
    const float cx = c.aspect() * 0.5f, cy = 0.47f, r = 0.33f;
    const ScreenColor ring{0.0f, 0.5f, 0.35f, 1}, faint{0.0f, 0.3f, 0.22f, 1};
    // rings at 1K, 10K, 100K units (log scale) plus the rim
    for (float d : {1000.0f, 10000.0f, 100000.0f}) c.circle(cx, cy, r * radarFraction(d, range_), 0.008f, faint, 40);
    c.circle(cx, cy, r, 0.014f, ring, 40);
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

    // contacts: needs the star system and our position (both optional; without them the scope stays empty)
    const world::IStarSystem* sys = eng_.services.get<world::IStarSystem>();
    const ship::IShip* ship = eng_.services.get<ship::IShip>();
    if (!sys || !ship || sys->bodies().empty()) {
        c.textCentered(cx, cy + r * 0.45f, "NO CONTACTS", 0.065f, {0.0f, 0.75f, 0.5f, 1});
        return;
    }

    // orientation: the published pose has forward AND up; IShip only has forward (world up is assumed then)
    engine::Vec3 fwd = ship->forward(), up{0, 1, 0};
    if (const core::ITransformSource* src = eng_.services.get<core::ITransformSource>()) {
        core::Pose pose = src->transform(eng_.alpha());
        fwd = pose.fwd; up = pose.up;
    }
    const RadarFrame frame = radarFrame(fwd, up);
    const engine::Vec3 sp = ship->position();
    const double sx = sp.x, sy = sp.y, sz = sp.z;

    RadarContacts contacts;
    int nearest = -1;
    float nearestSurface = 0, nearestHeight = 0, nearestDist = 0;
    const auto& bodies = sys->bodies();
    for (const world::Body& b : bodies) {
        world::Vec3d p = sys->positionAt(b.id);
        // subtract in double, only the (small enough) difference becomes float
        engine::Vec3 rel{(float)(p.x - sx), (float)(p.y - sy), (float)(p.z - sz)};
        RadarContact k;
        k.body = b.id;
        k.plot = radarPlot(rel, frame, range_);
        float surface = std::max(0.0f, k.plot.dist - b.radius);
        if (nearest < 0 || surface < nearestSurface) { nearest = b.id; nearestSurface = surface; nearestHeight = k.plot.height; nearestDist = k.plot.dist; }
        contacts.offer(k);
    }

    // asteroids first, so bodies and stations draw over them: the 12 nearest within cockpit.radar_asteroid_range, tiny dim dots, no stems
    if (const world::IAsteroids* ast = eng_.services.get<world::IAsteroids>()) {
        if (rockRate_.tick(eng_.time())) {
            rockIds_.clear(); ast->nearest({sx, sy, sz}, kMaxRadarAsteroids, rockIds_);
            static const std::string perk = combat::kScannerPerk;
            const gameplay::IInventory* inv = eng_.services.get<gameplay::IInventory>();
            scanner_ = inv && inv->hasPerk(perk);                  // Ore Scanner: re-checked with the rock list, a few times a second
        }
        int drawn = 0;
        for (int id : rockIds_) {
            if (drawn >= kMaxRadarAsteroids) break;
            world::Vec3d p = ast->position(id);
            RadarPlot pl = radarPlot({(float)(p.x - sx), (float)(p.y - sy), (float)(p.z - sz)}, frame, range_);
            if (!asteroidInRange(pl.dist, asteroidRange_)) continue;
            RockDot d = scanner_ ? rockDot(ast->ore(id), asteroidSizeTier(ast->radius(id))) : radarRockDot(asteroidSizeTier(ast->radius(id)), false, false, nullptr, 0);
            float size = asteroidDotSize(d.tier);
            c.rect(cx + pl.x * r - size * 0.5f, cy - pl.y * r - size * 0.5f, size, size, {d.r, d.g, d.b, 1});
            drawn++;
        }
    }

    for (int i = 0; i < contacts.count; i++) {
        const RadarContact& k = contacts.items[i];
        const world::Body& b = bodies[(size_t)k.body];
        ScreenColor col;
        float size;
        switch (b.kind) {
            case world::BodyKind::Sun:    col = {1.0f, 0.6f, 0.1f, 1}; size = 0.045f; break;
            case world::BodyKind::Planet: col = {std::max(b.color[0], 0.35f), std::max(b.color[1], 0.35f), std::max(b.color[2], 0.35f), 1}; size = 0.03f; break;
            default:                      col = {b.color[0] * 0.55f, b.color[1] * 0.55f, b.color[2] * 0.55f, 1}; size = 0.018f; break;
        }
        if (k.plot.clamped) size *= 0.6f;                       // beyond range: a small dot on the rim
        float px = cx + k.plot.x * r, py = cy - k.plot.y * r;   // flat position; forward is up on the scope
        if (k.plot.stem != 0) {
            // height stem: from the flat base to the dot, brighter when the body is above the ship's plane, dimmer when below
            const float bright = k.plot.stem > 0 ? 1.0f : 0.45f;
            float dy = py - k.plot.stem * r;
            c.line(px, py, px, dy, 0.009f, {col.r * bright, col.g * bright, col.b * bright, 1});
            c.frame(px - 0.010f, py - 0.010f, 0.020f, 0.020f, 0.005f, {0.0f, 0.6f, 0.4f, 1});   // base marker on the plane
            py = dy;
        }
        c.rect(px - size * 0.5f, py - size * 0.5f, size, size, col);
    }

    // stations: cyan diamonds with the same log range and height stems; filled when docking works for that station (the HUD's DOCK [G])
    if (const world::IStations* st = eng_.services.get<world::IStations>()) {
        std::string dockName, why;
        float dockDist = 0;
        bool dockOk = false;
        const ship::IDocking* dock = eng_.services.get<ship::IDocking>();
        const bool dockKnown = dock && dock->nearestDockable(dockName, dockDist, dockOk, why);
        RadarStations list;
        const int n = st->count();
        for (int i = 0; i < n; i++) {
            world::Vec3d p = st->info(i).position;
            RadarContact k;
            k.body = i;
            k.plot = radarPlot({(float)(p.x - sx), (float)(p.y - sy), (float)(p.z - sz)}, frame, range_);
            list.offer(k);
        }
        const ScreenColor cyan{0.2f, 0.9f, 1.0f, 1};
        int nearestStation = -1;
        float nearestStationDist = 0;
        for (int i = 0; i < list.count; i++) {
            const RadarContact& k = list.items[i];
            world::StationInfo info = st->info(k.body);
            StationMarker m = stationMarker(info.name, dockKnown, dockName, dockOk, k.plot.clamped);
            float px = cx + k.plot.x * r, py = cy - k.plot.y * r;
            if (k.plot.stem != 0) {
                const float bright = k.plot.stem > 0 ? 1.0f : 0.45f;
                float dy = py - k.plot.stem * r;
                c.line(px, py, px, dy, 0.009f, {cyan.r * bright, cyan.g * bright, cyan.b * bright, 1});
                c.frame(px - 0.010f, py - 0.010f, 0.020f, 0.020f, 0.005f, {0.0f, 0.6f, 0.4f, 1});
                py = dy;
            }
            c.diamond(px, py, m.size, 0.008f, cyan, m.filled);
            if (nearestStation < 0 || k.plot.dist < nearestStationDist) { nearestStation = k.body; nearestStationDist = k.plot.dist; }
        }
        if (nearestStation >= 0) {   // second label line under the body label: "STATION 1  420"
            world::StationInfo info = st->info(nearestStation);
            std::string line = radarShortName(info.name) + "  " + radarDistanceText(std::max(0.0f, nearestStationDist - info.half));
            float th = std::min(0.042f, (c.aspect() - 0.12f) / ((float)line.size() * kCellAspect * (1.0f + kGapRatio)));
            c.textCentered(cx, 0.925f, line, th, {0.2f, 0.8f, 0.9f, 1});
        }
    }

    if (nearest >= 0) {
        std::string label = bodies[(size_t)nearest].name + "  " + radarDistanceText(nearestSurface);
        std::string h = radarHeightText(nearestHeight, nearestDist);
        if (!h.empty()) label += "  " + h;
        float th = std::min(0.07f, (c.aspect() - 0.12f) / (label.size() * kCellAspect * (1.0f + kGapRatio)));   // shrink to fit long names
        c.textCentered(cx, 0.835f, label, th, {0.0f, 0.8f, 0.55f, 1});
    }
}

} // namespace cockpit
