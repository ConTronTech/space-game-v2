// ui/ship_hud - the flight HUD: speed, HP / shield / warp-fuel bars, crosshair, controls hint, IMPACT flash, warnings, destroyed screen.
// Reads state only through ship::IShip and its events. With no IShip (ship module off) it draws nothing and logs one warning.
// Layout and thresholds live in hud_logic.h (unit-tested). See docs/HUD.md.
#include <cstdio>
#include <map>
#include "core/ui_handler/ui_handler.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "core/data_registry/data_api.h"
#include "gameplay/inventory/inventory_api.h"
#include "gameplay/mining/mining_api.h"
#include "ship/cockpit/cockpit_screens_api.h"
#include "combat/weapons/weapons_api.h"
#include "ship/docking/docking_api.h"
#include "ship/orbit_lock/orbit_lock_api.h"
#include "ship/respawn/respawn_api.h"
#include "ship/ship_core/ship_api.h"
#include "ui/ship_hud/hud_logic.h"
#include "world/asteroids/asteroids_api.h"
#include "world/stations/stations_api.h"

class ShipHud : public engine::Module {
public:
    const char* name() const override { return "ui/ship_hud"; }
    std::vector<std::string> dependencies() const override { return {"core/ui_handler"}; }
    std::vector<std::string> optionalDependencies() const override { return {"ship/ship_core", "ship/fake_ship", "ship/cockpit", "ship/respawn"}; }

    bool init(engine::Engine& eng) override {
        eng_ = &eng;
        ui_ = &eng.services.require<core::UIHandler>();
        eng.events.subscribe<ship::DamageTaken>([this](const ship::DamageTaken& e) { state_.onDamage(e.amount, e.absorbedByShield, e.source, eng_->time()); });
        eng.events.subscribe<ship::ShieldBroken>([this](const ship::ShieldBroken&) { state_.onShieldBroken(eng_->time()); });
        eng.events.subscribe<ship::FuelEmpty>([this](const ship::FuelEmpty&) { state_.onFuelEmpty(eng_->time()); });
        eng.events.subscribe<ship::Respawned>([this](const ship::Respawned&) { state_.onRespawned(); });
        eng.events.subscribe<ship::OrbitLockChanged>([this](const ship::OrbitLockChanged& e) { state_.onOrbitLock(e.locked, e.bodyName, eng_->time()); });
        eng.events.subscribe<ship::Docked>([this](const ship::Docked& e) { state_.onDocked(e.stationName, eng_->time()); });
        eng.events.subscribe<ship::Undocked>([this](const ship::Undocked&) { state_.onUndocked(eng_->time()); });
        eng.events.subscribe<combat::Overheated>([this](const combat::Overheated& e) { state_.onOverheated(e.name, eng_->time()); });
        eng.events.subscribe<combat::WeaponChanged>([this](const combat::WeaponChanged& e) { state_.onWeaponChanged(e.name, eng_->time()); });
        eng.events.subscribe<world::AsteroidDestroyed>([this](const world::AsteroidDestroyed&) { state_.onEnemyKilled(eng_->time()); });
        eng.events.subscribe<gameplay::OreMined>([this](const gameplay::OreMined& e) {
            const OreInfo& o = oreInfo(e.ore);
            state_.onOreMined(e.ore, o.label, o.colour, e.amount, eng_->time());
        });
        eng.events.subscribe<gameplay::CargoFull>([this](const gameplay::CargoFull&) { state_.onCargoFull(eng_->time()); });
        dockRange_ = eng.config.get("docking.prompt_range", 0.0f, "distance from a station centre at which the HUD shows the dock prompt, units (0 = 4 x the station's dock radius)");
        auto parsed = hud::parseOverlayMode(eng.config.get<std::string>("hud.cockpit_overlay", "minimal",
            "flat HUD while the cockpit's own screens are visible: minimal (crosshair, warnings, hint; speed/bars live on the ship) | full (everything) | hidden (only hit vignette + destroyed screen)"));
        if (parsed.unknown) LOG_W("ship_hud", "hud.cockpit_overlay: unknown value, using 'minimal' (valid: minimal, full, hidden)");
        mode_ = parsed.mode;
        hintSeconds_ = eng.config.get("hud.hint_seconds", 20.0f, "seconds the controls hint is shown at full opacity before it fades out over 2 s (0 = never fade)");
        ui_->addPanel("ui/ship_hud", 10, [this](core::UIHandler& ui) { draw(ui); });
        return true;
    }
    void shutdown(engine::Engine&) override { ui_->removePanel("ui/ship_hud"); }

private:
    static constexpr hud::RGB kCalm{0.45f, 0.85f, 1.0f}, kReady{0.45f, 1.0f, 0.55f}, kDim{0.62f, 0.70f, 0.80f};
    static core::Color col(const hud::RGB& c, float a = 1) { return {c.r, c.g, c.b, a}; }

    void draw(core::UIHandler& ui) {
        auto* ship = eng_->services.get<ship::IShip>();
        if (!ship) {
            if (!warned_) { LOG_W("ship_hud", "no ship::IShip service (ship module off?): the HUD draws nothing"); warned_ = true; }
            return;
        }
        const ship::ShipStatus& st = ship->status();
        double now = eng_->time();
        auto* screens = eng_->services.get<cockpit::ICockpitScreens>();
        const hud::OverlayPlan plan = hud::overlayPlan(mode_, screens ? screens->showsDefaultUI() : true);
        const int W = ui.width(), H = ui.height();
        hud::Layout L = hud::computeLayout(W, H, st.shieldInstalled ? 3 : 2);
        const float s = L.scale;
        const int fSmall = (int)std::round(12 * s), fBig = (int)std::round(22 * s), fBar = (int)std::round(12 * s);

        // speed
        if (plan.speed) {
            // a live number makes a new text texture per change: refresh it 10x a second, not every frame
            if (now - speedAt_ >= 0.1 || speedAt_ > now) {
                char buf[64];
                std::snprintf(buf, sizeof buf, "%.1f m/s", st.speed);
                speedText_ = buf; speedAt_ = now;
            }
            ui.glass(L.speed.x, L.speed.y, L.speed.w, L.speed.h);
            ui.text(L.speed.x + 16 * s, L.speed.y + 6 * s, kSpeedLabel, fSmall, ui.theme.textDim);
            ui.text(L.speed.x + 16 * s, L.speed.y + 22 * s, speedText_, fBig, ui.theme.accent);
        }

        // bars
        if (plan.bars) {
            ui.glass(L.bars.x, L.bars.y, L.bars.w, L.bars.h);
            float y = L.bars.y + L.barPad;
            auto row = [&](const char* label, float cur, float max, hud::Bar kind) {
                float f = hud::fraction(cur, max);
                float lw = 46 * s, bx = L.bars.x + 12 * s + lw, bw = L.bars.w - 24 * s - lw, bh = 10 * s;
                ui.text(L.bars.x + 12 * s, y + (L.barRowH - fBar * 1.3f) / 2, label, fBar, ui.theme.textDim);
                ui.bar(bx, y + (L.barRowH - bh) / 2, bw, bh, f, col(hud::barColor(kind, f), 0.95f));
                y += L.barRowH;
            };
            row("HULL", st.hp, st.maxHp, hud::Bar::Hp);
            if (st.shieldInstalled) row("SHLD", st.shield, st.maxShield, hud::Bar::Shield);
            row("WARP", st.warpFuel, st.maxWarpFuel, hud::Bar::Fuel);
        }

        auto* combat = eng_->services.get<combat::ICombat>();          // optional: no weapons module = no weapon HUD
        auto* dockSvc = eng_->services.get<ship::IDocking>();
        const bool busy = dockSvc && dockSvc->busy();

        // crosshair (gap in the middle, thin arms)
        if (st.alive && plan.crosshair) {
            float g = 4 * s, r = L.crossR, t = std::max(1.0f, s);
            core::Color c = ui.theme.text; c.a = 0.75f;
            ui.rect(L.cx - g - r, L.cy - t / 2, r, t, c.r, c.g, c.b, c.a);
            ui.rect(L.cx + g, L.cy - t / 2, r, t, c.r, c.g, c.b, c.a);
            ui.rect(L.cx - t / 2, L.cy - g - r, t, r, c.r, c.g, c.b, c.a);
            ui.rect(L.cx - t / 2, L.cy + g, t, r, c.r, c.g, c.b, c.a);
            if (combat) drawWeaponCrosshair(ui, L, now, busy);
        }

        // hit flash: a faint red edge plus the IMPACT banner, fading over ~0.7 s
        float ia = state_.impactAlpha(now);
        if (ia > 0 && st.alive && plan.vignette) ui.vignette({1.0f, 0.15f, 0.1f, 0.5f * state_.impactSeverity() * ia}, 90 * s);

        // warning banners, stacked from the top centre (the impact banner sits above them)
        float by = L.bannerY;
        if (ia > 0 && st.alive && plan.banners) {
            drawBanner(ui, L, by, hud::impactText(state_.impactAmount(), state_.impactSource(), state_.impactShielded()), {1.0f, 0.5f, 0.4f}, ia);
            by += L.bannerH + L.bannerGap;
        }
        hud::Snapshot snap{st.hp, st.maxHp, st.warpFuel, st.maxWarpFuel, st.alive};
        if (plan.banners) for (auto& b : state_.banners(snap, now)) {
            hud::RGB c = (b.kind == hud::Warn::LowHp || b.kind == hud::Warn::FuelEmpty) ? hud::RGB{1.0f, 0.35f, 0.3f}
                       : (b.kind == hud::Warn::OrbitReleased || b.kind == hud::Warn::Docked || b.kind == hud::Warn::Undocked || b.kind == hud::Warn::WeaponChanged) ? kCalm : hud::RGB{1.0f, 0.75f, 0.25f};
            drawBanner(ui, L, by, b.text, c, b.alpha);
            by += L.bannerH + L.bannerGap;
        }
        // persistent status while orbit-locked (calm colour, not flashing, does not expire)
        if (plan.banners && st.alive && state_.orbitLocked()) {
            drawBanner(ui, L, by, state_.orbitStatus(), kCalm, 1.0f);
            by += L.bannerH + L.bannerGap;
        }
        // "+6 CRYSTAL" pickup banners (merged per ore, fade over ~1.5 s), stacked under the other top banners
        if (plan.banners && st.alive)
            state_.forEachPickup(now, [&](const std::string& text, const hud::RGB& c, float a) {
                drawBanner(ui, L, by, text, c, a);
                by += L.bannerH + L.bannerGap;
            });
        // docked line, or the station prompt while flying near one (both optional: no ship/docking = nothing)
        if (plan.banners && st.alive) {
            if (state_.docked()) {
                drawBanner(ui, L, by, state_.dockedStatus(), kCalm, 1.0f);
                by += L.bannerH + L.bannerGap;
            } else if (auto* dock = busy ? nullptr : dockSvc) {      // approaching the pad (busy, not yet docked): no prompt
                dq_.has = dock->nearestDockable(dq_.name, dq_.distance, dq_.ok, dq_.reason);
                if (dq_.has) {
                    hud::DockPrompt p = hud::dockPrompt(dq_, stationRadius(dq_.name), dockRange_, false);
                    if (p.show) {
                        drawBanner(ui, L, by, p.status, kCalm, 1.0f);
                        by += L.bannerH + L.bannerGap;
                        drawBanner(ui, L, by, p.action, p.ready ? kReady : kDim, p.ready ? 1.0f : 0.7f);
                        by += L.bannerH + L.bannerGap;
                    }
                }
            }
        }

        // bottom-left block: cargo row and weapon rows in one panel (each optional)
        if (plan.banners && st.alive) drawLeftBlock(ui, L, combat, eng_->services.get<gameplay::IInventory>(), now, busy);

        // controls hint (kept from the old demo HUD, plus V camera)
        float ha = plan.hint ? hud::hintAlpha(now, hintSeconds_) : 0.0f;
        if (ha > 0) {
            if (hintW_ != W || hintH_ != H) {                       // the fit only changes with the window size
                hintFont_ = hud::fitHint(L, W, [&](const std::string& t, int sz) { return ui.textWidth(t, sz); }, kHint);
                hintW_ = W; hintH_ = H; hintX_ = L.hint.x; hintWidth_ = L.hint.w;
            }
            L.hint.x = hintX_; L.hint.w = hintWidth_;
            ui.glass(L.hint.x, L.hint.y, L.hint.w, L.hint.h, 0.9f * ha, false, 10 * s);
            core::Color hc = ui.theme.textDim; hc.a *= ha;
            ui.textCentered(W / 2.0f, L.hint.y + (L.hint.h - hintFont_ * 1.3f) / 2, kHint, hintFont_, hc);
        }

        // destroyed
        if (!st.alive && plan.destroyed) {
            ui.vignette({0.8f, 0.0f, 0.0f, 0.75f}, std::min(W, H) * 0.45f);
            ui.rect(0, 0, (float)W, (float)H, 0.25f, 0.0f, 0.0f, 0.25f);
            ui.textCentered(W / 2.0f, H * 0.42f, "SHIP DESTROYED", (int)std::round(44 * s), {1.0f, 0.3f, 0.25f, 1.0f});
            auto* rs = eng_->services.get<ship::IRespawn>();
            std::string rt = rs && rs->counting() ? hud::respawnText(rs->secondsLeft()) : "";
            if (!rt.empty()) ui.textCentered(W / 2.0f, H * 0.42f + 60 * s, rt, (int)std::round(22 * s), ui.theme.text);
        }
    }

    // heat bars either side of the crosshair (a few small quads), hit marker ticks around it
    void drawWeaponCrosshair(core::UIHandler& ui, const hud::Layout& L, double now, bool busy) {
        auto& cb = *eng_->services.get<combat::ICombat>();
        const float s = L.scale;
        int sel = cb.selected();
        float heat = hud::clamp01(cb.heat(sel));
        bool hot = cb.overheated(sel);
        float dim = busy ? 0.35f : 1.0f;
        float segH = 3 * s, gap = 1.5f * s, w = std::max(2.0f, 2 * s), x0 = L.crossR + 16 * s;
        int lit = hot ? hud::kHeatSegments : hud::heatSegments(heat);
        hud::RGB c = hot ? hud::RGB{1.0f, 0.25f, 0.22f} : hud::heatColor(heat);
        float a = hot ? hud::overheatFlash(now) : 0.9f;
        for (int i = 0; i < hud::kHeatSegments; i++) {
            float y = L.cy + (hud::kHeatSegments / 2.0f - 1 - i) * (segH + gap);        // bottom segment fills first
            bool on = i < lit;
            float al = (on ? a : 0.18f) * dim;
            hud::RGB k = on ? c : hud::RGB{0.6f, 0.7f, 0.8f};
            ui.rect(L.cx - x0 - w, y, w, segH, k.r, k.g, k.b, al);
            ui.rect(L.cx + x0, y, w, segH, k.r, k.g, k.b, al);
        }
        hud::HitMarker m = hud::hitMarker(cb.hitMarkerAge(), state_.killAge(now));
        if (m.alpha > 0) {
            hud::RGB k = m.kill ? hud::RGB{1.0f, 0.35f, 0.3f} : hud::RGB{1.0f, 1.0f, 1.0f};
            float d = 9 * s, len = 5 * s, t = std::max(1.5f, 1.5f * s);
            for (int sx = -1; sx <= 1; sx += 2)
                for (int sy = -1; sy <= 1; sy += 2) {           // an L-shaped tick in each diagonal corner
                    float cx = L.cx + sx * d, cy = L.cy + sy * d;
                    ui.rect(sx > 0 ? cx : cx - len, cy - t / 2, len, t, k.r, k.g, k.b, m.alpha);
                    ui.rect(cx - t / 2, sy > 0 ? cy : cy - len, t, len, k.r, k.g, k.b, m.alpha);
                }
        }
    }

    // bottom-left block, one row per weapon: "BLASTER  [1]" + mini heat bar; the selected row is bright, the others dim
    // Bottom-left block above the hint, ONE glass panel for everything it shows: an optional cargo row ("CARGO 45 / 100" + thin bar; amber above
    // 80 %, red when full) and, with combat/weapons, one row per weapon ("BLASTER  [1]" + mini heat bar; the selected one bright, the others dim).
    void drawLeftBlock(core::UIHandler& ui, const hud::Layout& L, combat::ICombat* cb, gameplay::IInventory* inv, double now, bool busy) {
        const float s = L.scale;
        int n = cb ? cb->weaponCount() : 0;
        if (cb && (int)labels_.size() != n) {                             // names never change at runtime: build the labels once
            labels_.clear();
            for (int i = 0; i < n; i++) labels_.push_back(hud::weaponLabel(cb->weaponName(i), i));
        }
        int rows = n + (inv ? 1 : 0);
        if (rows <= 0) return;
        float rowH = 26 * s, pad = 8 * s, w = 236 * s, h = pad * 2 + rowH * rows;
        float x = 16 * s, y = L.hint.y - 12 * s - h;
        int fs = (int)std::round(13 * s);
        float bx = x + 140 * s, bw = w - 152 * s;
        ui.glass(x, y, w, h, busy && !inv ? 0.6f : 0.9f);
        float ry = y + pad;
        if (inv) {
            float used = inv->used(), cap = inv->capacity();
            int u = (int)std::lround(used), c = (int)std::lround(cap);
            if (u != cargoU_ || c != cargoC_) { cargoU_ = u; cargoC_ = c; cargoText_ = hud::cargoText(used, cap); }   // rebuilt only when it changes
            float frac = hud::cargoFraction(used, cap);
            hud::RGB k = hud::cargoColor(frac);
            ui.text(x + 12 * s, ry + (rowH - fs * 1.3f) / 2, cargoText_, fs, col(k));
            float bh = 5 * s;
            ui.bar(bx, ry + (rowH - bh) / 2, bw, bh, frac, col(k, 0.95f));
            ry += rowH;
        }
        int sel = cb ? cb->selected() : -1;
        for (int i = 0; i < n; i++, ry += rowH) {
            hud::WeaponRow st = hud::weaponRowState(busy, cb->overheated(i), cb->heat(i));
            bool isSel = i == sel;
            float k = (isSel ? 1.0f : 0.45f) * (busy ? 0.6f : 1.0f);
            core::Color tc = isSel ? ui.theme.text : ui.theme.textDim;
            tc.a *= k;
            ui.text(x + 12 * s, ry + (rowH - fs * 1.3f) / 2, labels_[(size_t)i], fs, tc);
            const char* note = hud::weaponRowNote(st);
            float bh = 8 * s;
            if (*note) {
                core::Color nc = st == hud::WeaponRow::Locked ? core::Color{1.0f, 0.3f, 0.25f, hud::overheatFlash(now) * k} : core::Color{0.62f, 0.70f, 0.80f, k};
                ui.text(bx, ry + (rowH - fs * 1.3f) / 2, note, (int)std::round(11 * s), nc);
            } else {
                hud::RGB c = hud::heatColor(cb->heat(i));
                ui.bar(bx, ry + (rowH - bh) / 2, bw, bh, cb->heat(i), col(c, 0.95f * k));
            }
        }
    }

    void drawBanner(core::UIHandler& ui, const hud::Layout& L, float y, const std::string& text, const hud::RGB& c, float a) {
        int fs = (int)std::round(18 * L.scale);
        float w = std::max(L.bannerW, ui.textWidth(text, fs) + 40 * L.scale);
        ui.glass(L.cx - w / 2, y, w, L.bannerH, a, false, 10 * L.scale);
        ui.textCentered(L.cx, y + (L.bannerH - fs * 1.3f) / 2, text, fs, col(c, a));
    }

    // dock radius of the station called 'name' (world/stations, optional); cached by name so the lookup is not per frame
    float stationRadius(const std::string& name) {
        if (name == radiusName_) return radius_;
        radiusName_ = name;
        radius_ = 120.0f;                                     // smallest station dock zone when world/stations is off
        if (auto* stations = eng_->services.get<world::IStations>())
            for (int i = 0; i < stations->count(); i++) { auto info = stations->info(i); if (info.name == name) { radius_ = info.radius; break; } }
        return radius_;
    }

    engine::Engine* eng_ = nullptr;
    core::UIHandler* ui_ = nullptr;
    hud::HudState state_;
    struct OreInfo { std::string label; hud::RGB colour{0.45f, 0.85f, 1.0f}; };
    // display name and colour of an ore from data/ores.json (optional); looked up once per ore id, then cached
    const OreInfo& oreInfo(const std::string& id) {
        auto it = ores_.find(id);
        if (it != ores_.end()) return it->second;
        OreInfo o;
        std::string name;
        if (auto* data = eng_->services.get<core::IData>()) {
            const engine::Json& j = data->get("ores", id);
            name = j["name"].str("");
            if (j["color"].size() >= 3) o.colour = {(float)j["color"].at(0).num(0.45), (float)j["color"].at(1).num(0.85), (float)j["color"].at(2).num(1.0)};
        }
        o.label = hud::oreLabel(id, name);
        if (!(o.colour.r + o.colour.g + o.colour.b > 0)) o.colour = {0.45f, 0.85f, 1.0f};
        // ore colours are dark tints for rocks: brighten so the toast is readable on the glass
        o.colour = hud::lerp(o.colour, hud::RGB{1, 1, 1}, 0.45f);
        return ores_.emplace(id, std::move(o)).first->second;
    }

    static inline const std::string kHint = "W/S thrust   A/D strafe   Space/C up/down   mouse look   Q/E roll   X brake   Tab free mouse   V camera   Esc pause";
    static inline const std::string kSpeedLabel = "SPEED";
    std::map<std::string, OreInfo> ores_;
    std::string speedText_, cargoText_;
    double speedAt_ = -1;
    int cargoU_ = -1, cargoC_ = -1;
    int hintW_ = -1, hintH_ = -1, hintFont_ = 13;
    float hintX_ = 0, hintWidth_ = 0;
    bool warned_ = false;
    std::vector<std::string> labels_;
    float dockRange_ = 0;
    hud::DockQuery dq_;
    std::string radiusName_;
    float radius_ = 120.0f;
    hud::OverlayMode mode_ = hud::OverlayMode::Minimal;
    float hintSeconds_ = 20.0f;
};

REGISTER_MODULE(ShipHud);
