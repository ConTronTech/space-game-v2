// ui/ship_hud - the flight HUD: speed, HP / shield / warp-fuel bars, crosshair, controls hint, IMPACT flash, warnings, destroyed screen.
// Reads state only through ship::IShip and its events. With no IShip (ship module off) it draws nothing and logs one warning.
// Layout and thresholds live in hud_logic.h (unit-tested). See docs/HUD.md.
#include <cstdio>
#include "core/ui_handler/ui_handler.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "ship/ship_core/ship_api.h"
#include "ui/ship_hud/hud_logic.h"

class ShipHud : public engine::Module {
public:
    const char* name() const override { return "ui/ship_hud"; }
    std::vector<std::string> dependencies() const override { return {"core/ui_handler"}; }
    std::vector<std::string> optionalDependencies() const override { return {"ship/ship_core", "ship/fake_ship"}; }

    bool init(engine::Engine& eng) override {
        eng_ = &eng;
        ui_ = &eng.services.require<core::UIHandler>();
        eng.events.subscribe<ship::DamageTaken>([this](const ship::DamageTaken& e) { state_.onDamage(e.amount, e.source, eng_->time()); });
        eng.events.subscribe<ship::ShieldBroken>([this](const ship::ShieldBroken&) { state_.onShieldBroken(eng_->time()); });
        eng.events.subscribe<ship::FuelEmpty>([this](const ship::FuelEmpty&) { state_.onFuelEmpty(eng_->time()); });
        eng.events.subscribe<ship::Respawned>([this](const ship::Respawned&) { state_.onRespawned(); });
        ui_->addPanel("ui/ship_hud", 10, [this](core::UIHandler& ui) { draw(ui); });
        return true;
    }
    void shutdown(engine::Engine&) override { ui_->removePanel("ui/ship_hud"); }

private:
    static core::Color col(const hud::RGB& c, float a = 1) { return {c.r, c.g, c.b, a}; }

    void draw(core::UIHandler& ui) {
        auto* ship = eng_->services.get<ship::IShip>();
        if (!ship) {
            if (!warned_) { LOG_W("ship_hud", "no ship::IShip service (ship module off?): the HUD draws nothing"); warned_ = true; }
            return;
        }
        const ship::ShipStatus& st = ship->status();
        double now = eng_->time();
        const int W = ui.width(), H = ui.height();
        hud::Layout L = hud::computeLayout(W, H, st.shieldInstalled ? 3 : 2);
        const float s = L.scale;
        const int fSmall = (int)std::round(12 * s), fBig = (int)std::round(22 * s), fBar = (int)std::round(12 * s);

        // speed
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.1f m/s", st.speed);
        ui.glass(L.speed.x, L.speed.y, L.speed.w, L.speed.h);
        ui.text(L.speed.x + 16 * s, L.speed.y + 6 * s, "SPEED", fSmall, ui.theme.textDim);
        ui.text(L.speed.x + 16 * s, L.speed.y + 22 * s, buf, fBig, ui.theme.accent);

        // bars
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

        // crosshair (gap in the middle, thin arms)
        if (st.alive) {
            float g = 4 * s, r = L.crossR, t = std::max(1.0f, s);
            core::Color c = ui.theme.text; c.a = 0.75f;
            ui.rect(L.cx - g - r, L.cy - t / 2, r, t, c.r, c.g, c.b, c.a);
            ui.rect(L.cx + g, L.cy - t / 2, r, t, c.r, c.g, c.b, c.a);
            ui.rect(L.cx - t / 2, L.cy - g - r, t, r, c.r, c.g, c.b, c.a);
            ui.rect(L.cx - t / 2, L.cy + g, t, r, c.r, c.g, c.b, c.a);
        }

        // hit flash: a faint red edge plus the IMPACT banner, fading over ~0.7 s
        float ia = state_.impactAlpha(now);
        if (ia > 0 && st.alive) ui.vignette({1.0f, 0.15f, 0.1f, 0.35f * ia}, 90 * s);

        // warning banners, stacked from the top centre (the impact banner sits above them)
        float by = L.bannerY;
        if (ia > 0 && st.alive) {
            drawBanner(ui, L, by, hud::impactText(state_.impactAmount(), state_.impactSource()), {1.0f, 0.5f, 0.4f}, ia);
            by += L.bannerH + L.bannerGap;
        }
        hud::Snapshot snap{st.hp, st.maxHp, st.warpFuel, st.maxWarpFuel, st.alive};
        for (auto& b : state_.banners(snap, now)) {
            hud::RGB c = (b.kind == hud::Warn::LowHp || b.kind == hud::Warn::FuelEmpty) ? hud::RGB{1.0f, 0.35f, 0.3f} : hud::RGB{1.0f, 0.75f, 0.25f};
            drawBanner(ui, L, by, b.text, c, b.alpha);
            by += L.bannerH + L.bannerGap;
        }

        // controls hint (kept from the old demo HUD, plus V camera)
        const char* hint = "W/S thrust   A/D strafe   Space/C up/down   mouse look   Q/E roll   X brake   Tab free mouse   V camera   Esc pause";
        int hf = hud::fitHint(L, W, [&](const std::string& t, int sz) { return ui.textWidth(t, sz); }, hint);
        ui.glass(L.hint.x, L.hint.y, L.hint.w, L.hint.h, 0.9f, false, 10 * s);
        ui.textCentered(W / 2.0f, L.hint.y + (L.hint.h - hf * 1.3f) / 2, hint, hf, ui.theme.textDim);

        // destroyed
        if (!st.alive) {
            ui.vignette({0.8f, 0.0f, 0.0f, 0.75f}, std::min(W, H) * 0.45f);
            ui.rect(0, 0, (float)W, (float)H, 0.25f, 0.0f, 0.0f, 0.25f);
            ui.textCentered(W / 2.0f, H * 0.42f, "SHIP DESTROYED", (int)std::round(44 * s), {1.0f, 0.3f, 0.25f, 1.0f});
        }
    }

    void drawBanner(core::UIHandler& ui, const hud::Layout& L, float y, const std::string& text, const hud::RGB& c, float a) {
        int fs = (int)std::round(18 * L.scale);
        float w = std::max(L.bannerW, ui.textWidth(text, fs) + 40 * L.scale);
        ui.glass(L.cx - w / 2, y, w, L.bannerH, a, false, 10 * L.scale);
        ui.textCentered(L.cx, y + (L.bannerH - fs * 1.3f) / 2, text, fs, col(c, a));
    }

    engine::Engine* eng_ = nullptr;
    core::UIHandler* ui_ = nullptr;
    hud::HudState state_;
    bool warned_ = false;
};

REGISTER_MODULE(ShipHud);
