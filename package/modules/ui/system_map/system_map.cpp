// ui/system_map - the MAP tab of the game menu: a flat top-down schematic of the whole star system (log radial scale, pan/zoom).
// Pure rules in system_map_rules.h; see docs/SYSTEM_MAP.md. Every world service is optional: a missing one just draws "no data" for its part.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "core/input_handler/input_api.h"
#include "core/ui_handler/ui_handler.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "ship/gravity/gravity_rules.h"   // pure SOI rules (ship/gravity exposes no service): the SAME dominant-body choice, recomputed read-only
#include "ship/ship_core/ship_api.h"
#include "ui/game_menu/game_menu_api.h"
#include "ui/system_map/system_map_rules.h"
#include "world/anomalies/anomalies_api.h"
#include "world/asteroids/asteroids_api.h"
#include "world/star_system/star_system_api.h"
#include "world/stations/stations_api.h"

class SystemMap : public engine::Module {
public:
    const char* name() const override { return "ui/system_map"; }
    std::vector<std::string> dependencies() const override { return {"ui/game_menu", "core/ui_handler"}; }
    std::vector<std::string> optionalDependencies() const override {
        return {"core/input_handler", "world/star_system", "world/stations", "world/asteroids", "ship/ship_core", "ship/fake_ship", "ship/gravity", "gameplay/crafting"};
    }

    bool init(engine::Engine& eng) override {
        eng_ = &eng;
        auto& c = eng.config;
        lim_.zoomMin = c.get("system_map.zoom_min", 500.0f, "MAP tab: the closest zoom, units shown at the map's rim");
        lim_.zoomMax = c.get("system_map.zoom_max", 1000000.0f, "MAP tab: the farthest zoom, units shown at the map's rim (also how far the view can be panned from the sun)");
        defaultRange_ = c.get("system_map.default_zoom", 400000.0f, "MAP tab: the zoom it opens at, units at the rim (400,000 shows the whole seed-1234 system)");
        sunRange_ = std::max(0.0f, c.get("gravity.sun_range", 2.0f, "the sun pulls out to this many times the outermost planet's orbit radius; beyond it, no gravity at all"));
        lim_.panMax = lim_.zoomMax;
        view_.range = defaultRange_;
        view_ = sysmap::clampView(view_, lim_);
        // dev: --map-view=RANGE[,BODY]  opens the map at that zoom, focused on body id BODY (for screenshots)
        std::string mv = eng.flagValue("map-view");
        if (!mv.empty()) { view_.range = std::atof(mv.c_str()); auto k = mv.find(','); if (k != std::string::npos) focusBody_ = std::atoi(mv.c_str() + k + 1); view_ = sysmap::clampView(view_, lim_); }
        input_ = eng.services.get<core::IInput>();
        menu_ = &eng.services.require<ui::IGameMenu>();
        menu_->addTab("MAP", 30, [this](core::UIHandler& ui, float x, float y, float w, float h) { draw(ui, x, y, w, h); });
        return true;
    }

    void shutdown(engine::Engine&) override { if (menu_) menu_->removeTab("MAP"); }

private:
    static constexpr int kRingDots = 72;

    void dot(core::UIHandler& ui, sysmap::P2 p, float s, const float c[3], float a) { ui.rect(p.x - s, p.y - s, 2 * s, 2 * s, c[0], c[1], c[2], a); }
    void diamond(core::UIHandler& ui, sysmap::P2 p, float s, const float c[3]) {   // three stacked bars: a small diamond, like the radar's stations
        ui.rect(p.x - s, p.y - s * 0.33f, 2 * s, s * 0.66f, c[0], c[1], c[2], 1);
        ui.rect(p.x - s * 0.6f, p.y - s * 0.66f, s * 1.2f, s * 1.32f, c[0], c[1], c[2], 1);
        ui.rect(p.x - s * 0.25f, p.y - s, s * 0.5f, 2 * s, c[0], c[1], c[2], 1);
    }
    bool inside(sysmap::P2 p) const { return p.x >= x0_ && p.x <= x1_ && p.y >= y0_ && p.y <= y1_; }
    // beyond the rim: not drawn (the log map would pile everything farther away onto the rim circle)
    bool beyond(double x, double z) const { return std::hypot(x - sun_.x - view_.fx, z - sun_.z - view_.fz) > view_.range; }

    // A thin orbit ring: kRingDots points of the circle (centre (ox, oz), radius r, world XZ), each projected (the log map bends off-focus circles).
    void ring(core::UIHandler& ui, double ox, double oz, double r, float s, const float c[3], float a) {
        for (int i = 0; i < kRingDots; i++) {
            double t = i * (6.283185307179586 / kRingDots);
            double px = ox + r * std::cos(t), pz = oz + r * std::sin(t);
            if (beyond(px, pz)) continue;
            sysmap::P2 p = proj(px, pz);
            if (inside(p)) ui.rect(p.x - s * 0.5f, p.y - s * 0.5f, s, s, c[0], c[1], c[2], a);
        }
    }
    sysmap::P2 proj(double x, double z) const { return sysmap::project(x, z, sun_.x + view_.fx, sun_.z + view_.fz, view_.range, cx_, cy_, R_); }

    void button(core::UIHandler& ui, const char* label, float x, float y, float w, float h, int zoom, int px, int pz) {
        if (!ui.button(label, x, y, w, h, false)) return;
        focusBody_ = -1;
        view_ = sysmap::panned(sysmap::zoomed(view_, zoom, lim_), px, pz, lim_);
    }

    void draw(core::UIHandler& ui, float x, float y, float w, float h) {
        float us = core::uiLayoutScale(ui.width(), ui.height());
        auto* sys = eng_->services.get<world::IStarSystem>();
        if (input_) {
            if (input_->pressed("ui_up")) view_ = sysmap::zoomed(view_, +1, lim_);
            if (input_->pressed("ui_down")) view_ = sysmap::zoomed(view_, -1, lim_);
        }
        // controls row
        float bh = 28, bw = 40, bx = x;
        button(ui, "+", bx, y, bw, bh, +1, 0, 0); bx += bw + 4;
        button(ui, "-", bx, y, bw, bh, -1, 0, 0); bx += bw + 12;
        button(ui, "<", bx, y, bw, bh, 0, -1, 0); bx += bw + 4;
        button(ui, "^", bx, y, bw, bh, 0, 0, -1); bx += bw + 4;
        button(ui, "v", bx, y, bw, bh, 0, 0, +1); bx += bw + 4;
        button(ui, ">", bx, y, bw, bh, 0, +1, 0); bx += bw + 12;
        if (ui.button("SUN", bx, y, 60, bh, false)) { focusBody_ = -1; view_ = sysmap::clampView({defaultRange_, 0, 0}, lim_); }
        bx += 64;
        if (ui.button("SHIP", bx, y, 64, bh, false)) { focusBody_ = -2; view_.range = std::min(view_.range, 20000.0); view_ = sysmap::clampView(view_, lim_); }
        bx += 72;
        char zb[64]; std::snprintf(zb, sizeof zb, "rim %.0f units   (Up/Down: zoom)", view_.range);
        ui.text(bx, y + 6, zb, 13, ui.theme.textDim);

        float my = y + bh + 8, mh = h - bh - 8 - 22;
        x0_ = x; x1_ = x + w; y0_ = my; y1_ = my + mh;
        cx_ = x + w / 2; cy_ = my + mh / 2; R_ = std::min(w, mh) / 2 - 6;
        ui.rect(x, my, w, mh, 0.01f, 0.02f, 0.04f, 0.9f);
        const float dim[3] = {0.35f, 0.5f, 0.65f};
        if (!sys || sys->bodies().empty()) { ui.textCentered(cx_, cy_ - 10, "no data (no star system)", 16, ui.theme.textDim); return; }
        const auto& bodies = sys->bodies();
        sun_ = sys->sunPosition();
        ship::IShip* ship = eng_->services.get<ship::IShip>();
        world::Vec3d sp{};
        if (ship) { auto p = ship->position(); sp = {p.x, p.y, p.z}; }
        // focus follows a body / the ship when one was picked
        if (focusBody_ >= 0 && focusBody_ < (int)bodies.size()) { view_.fx = bodies[(size_t)focusBody_].position.x - sun_.x; view_.fz = bodies[(size_t)focusBody_].position.z - sun_.z; }
        else if (focusBody_ == -2 && ship) { view_.fx = sp.x - sun_.x; view_.fz = sp.z - sun_.z; }
        view_ = sysmap::clampView(view_, lim_);

        // range rings (faint, every factor of 10 from the focus) + asteroid belt band
        const float grid[3] = {0.2f, 0.3f, 0.4f};
        for (double r = 1000; r < view_.range; r *= 10) ring(ui, sun_.x + view_.fx, sun_.z + view_.fz, r, 1.0f * us, grid, 0.35f);
        if (auto* ast = eng_->services.get<world::IAsteroids>()) {
            if (!beltDone_) {   // asteroids are static: measured once
                beltDone_ = true;
                std::vector<double> rs; rs.reserve((size_t)ast->count());
                for (int i = 0; i < ast->count(); i++) { auto p = ast->position(i); rs.push_back(std::hypot(p.x - sun_.x, p.z - sun_.z)); }
                band_ = sysmap::beltBand(rs);
            }
            if (band_.valid) {
                const float bc[3] = {0.55f, 0.48f, 0.38f};
                for (int k = 0; k <= 4; k++) ring(ui, sun_.x, sun_.z, band_.inner + (band_.outer - band_.inner) * k / 4.0, 2.0f * us, bc, 0.35f);
            }
        }

        // SOI / dominant body (only while ship/gravity is running: it is what the ship actually feels)
        int dom = -1;
        if (ship && eng_->findModule("ship/gravity")) {
            gravity::buildSources(bodies, 60.0, sunRange_, src_);   // the SOI ratio does not depend on orbit.gravity_scale (it cancels)
            dom = gravity::dominantBody(src_, sp, lastDom_, 1.05);
            lastDom_ = dom;
        }

        // orbits, then bodies (moons spread out from their planet so they stay visible at system zoom)
        for (const auto& b : bodies) {
            if (b.parent < 0 || b.parent >= (int)bodies.size()) continue;
            const auto& par = bodies[(size_t)b.parent];
            if (b.kind == world::BodyKind::Moon) {   // a moon ring only when it is big enough on screen to mean something
                sysmap::P2 a = proj(par.position.x, par.position.z), e = proj(par.position.x + b.orbitRadius, par.position.z);
                if (std::hypot(e.x - a.x, e.y - a.y) < 12.0f * us) continue;
            }
            ring(ui, par.position.x, par.position.z, b.orbitRadius, 1.6f * us, dim, b.kind == world::BodyKind::Moon ? 0.6f : 0.8f);
        }
        std::string hoverInfo;
        int moonIdx[64] = {0};
        for (const auto& b : bodies) {
            float c[3];
            sysmap::markerColor(b.color, 0.55f, c);
            sysmap::Mark m = b.kind == world::BodyKind::Sun ? sysmap::Mark::Sun : b.kind == world::BodyKind::Moon ? sysmap::Mark::Moon : sysmap::Mark::Planet;
            float s = sysmap::markerSize(m, b.radius) * us;
            if (beyond(b.position.x, b.position.z)) continue;
            sysmap::P2 p = proj(b.position.x, b.position.z);
            if (m == sysmap::Mark::Moon && b.parent >= 0 && b.parent < (int)bodies.size()) {
                const auto& par = bodies[(size_t)b.parent];
                sysmap::P2 pp = proj(par.position.x, par.position.z);
                float dx = p.x - pp.x, dy = p.y - pp.y, d = std::hypot(dx, dy);
                int idx = b.parent < 64 ? moonIdx[b.parent]++ : 0;
                float minD = sysmap::moonMinOffset(sysmap::markerSize(sysmap::Mark::Planet, par.radius) * us, idx);
                if (d < minD) {
                    double wx = b.position.x - par.position.x, wz = b.position.z - par.position.z, wd = std::hypot(wx, wz);
                    float ux = wd > 1e-9 ? (float)(wx / wd) : 1.0f, uy = wd > 1e-9 ? (float)(wz / wd) : 0.0f;
                    p = {pp.x + ux * minD, pp.y + uy * minD};
                }
            }
            if (!inside(p)) continue;
            if (b.id == dom) {   // the SOI the ship is in: a bright ring round the marker + (if it fits) the SOI circle itself
                const float hc[3] = {0.35f, 0.75f, 1.0f};
                if (dom < (int)src_.size() && m != sysmap::Mark::Sun) ring(ui, b.position.x, b.position.z, src_[(size_t)dom].soi, 1.5f * us, hc, 0.7f);
                dot(ui, p, s + 3 * us, hc, 0.5f);
            }
            dot(ui, p, s, c, 1.0f);
            if (m != sysmap::Mark::Moon || view_.range < 60000) ui.text(p.x + s + 3, p.y - 7, b.name, 12, ui.theme.textDim);
            if (ui.hovered(p.x - s - 3, p.y - s - 3, 2 * s + 6, 2 * s + 6)) hoverInfo = info(b.name, m == sysmap::Mark::Sun ? "sun" : m == sysmap::Mark::Moon ? "moon" : "planet", b.position, sp, ship != nullptr);
        }
        if (auto* st = eng_->services.get<world::IStations>()) {
            const float sc[3] = {0.4f, 1.0f, 0.75f};
            for (int i = 0; i < st->count(); i++) {
                world::StationInfo si = st->info(i);
                if (beyond(si.position.x, si.position.z)) continue;
                sysmap::P2 p = proj(si.position.x, si.position.z);
                if (si.parent >= 0 && si.parent < (int)bodies.size()) {   // pushed out of its planet's marker, like a moon
                    const auto& par = bodies[(size_t)si.parent];
                    sysmap::P2 pp = proj(par.position.x, par.position.z);
                    float minD = sysmap::markerSize(sysmap::Mark::Planet, par.radius) * us + 5.0f * us;
                    if (std::hypot(p.x - pp.x, p.y - pp.y) < minD) p = {pp.x - minD * 0.7071f, pp.y - minD * 0.7071f};
                }
                if (!inside(p)) continue;
                float s = sysmap::markerSize(sysmap::Mark::Station, 0) * us;
                diamond(ui, p, s, sc);
                if (ui.hovered(p.x - s - 3, p.y - s - 3, 2 * s + 6, 2 * s + 6)) hoverInfo = info(si.name, si.kind == world::StationKind::Orbital ? "orbital station" : "planetary station", si.position, sp, ship != nullptr);
            }
        }
        // anomalies (world/anomalies, optional): only the DETECTED ones (scanner perk + in range + not investigated), exactly like the radar
        if (auto* an = eng_->services.get<world::IAnomalies>()) {
            const float s = sysmap::markerSize(sysmap::Mark::Anomaly, 0) * us, t = std::max(1.0f, 1.5f * us);
            for (int i = 0; i < an->count(); i++) {
                if (!an->detected(i)) continue;
                world::Vec3d ap = an->position(i);
                if (beyond(ap.x, ap.z)) continue;
                sysmap::P2 p = proj(ap.x, ap.z);
                if (!inside(p)) continue;
                const float* vc = sysmap::kAnomalyColor;   // a small 4-point star: two thin crossed bars
                ui.rect(p.x - s, p.y - t * 0.5f, 2 * s, t, vc[0], vc[1], vc[2], 1);
                ui.rect(p.x - t * 0.5f, p.y - s, t, 2 * s, vc[0], vc[1], vc[2], 1);
                if (ui.hovered(p.x - s - 3, p.y - s - 3, 2 * s + 6, 2 * s + 6)) hoverInfo = info("Anomaly", "unidentified signal", ap, sp, ship != nullptr);
            }
        }
        if (ship) {
            sysmap::P2 p = proj(sp.x, sp.z);
            if (inside(p) && !beyond(sp.x, sp.z)) {
                const float wc[3] = {1.0f, 1.0f, 1.0f}, yc[3] = {1.0f, 0.85f, 0.2f};
                float s = sysmap::markerSize(sysmap::Mark::Ship, 0) * us;
                dot(ui, p, s + 1.5f * us, yc, 1.0f);
                dot(ui, p, s * 0.5f, wc, 1.0f);
            }
        }
        // info line
        std::string line = hoverInfo;
        if (line.empty()) {
            line = ship ? (dom >= 0 ? "Ship in the sphere of influence of " + bodies[(size_t)dom].name : std::string(eng_->findModule("ship/gravity") ? "Ship in deep space" : "Ship position (no gravity data)"))
                        : std::string("no ship data");
            if (!eng_->services.get<world::IStations>()) line += "   -   no station data";
            line += "   -   hover a marker for details";
        }
        ui.text(x, y + h - 18, line, 13, ui.theme.text);
    }

    static std::string info(const std::string& name, const char* kind, world::Vec3d p, world::Vec3d ship, bool haveShip) {
        char b[160];
        if (haveShip) std::snprintf(b, sizeof b, "%s  (%s)  -  %.0f units from the ship", name.c_str(), kind, std::sqrt((p.x - ship.x) * (p.x - ship.x) + (p.y - ship.y) * (p.y - ship.y) + (p.z - ship.z) * (p.z - ship.z)));
        else std::snprintf(b, sizeof b, "%s  (%s)", name.c_str(), kind);
        return b;
    }

    engine::Engine* eng_ = nullptr;
    core::IInput* input_ = nullptr;
    ui::IGameMenu* menu_ = nullptr;
    sysmap::View view_;
    sysmap::Limits lim_;
    double defaultRange_ = 400000;
    float sunRange_ = 2.0f;
    int focusBody_ = -1;                 // >= 0 a body id the view follows, -2 the ship, -1 free
    world::Vec3d sun_;
    float cx_ = 0, cy_ = 0, R_ = 1, x0_ = 0, x1_ = 0, y0_ = 0, y1_ = 0;
    bool beltDone_ = false;
    sysmap::Band band_;
    std::vector<gravity::Source> src_;   // sized once, reused
    int lastDom_ = -1;
};

REGISTER_MODULE(SystemMap);
