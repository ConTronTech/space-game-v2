// ui/quick_action_bar - a No Man's Sky-style quick menu for flight: a strip of secondary actions opened with one key (B), scrolled with
// , / . and fired with Return, WITHOUT pausing the game. See docs/QUICK_ACTION_BAR.md; the pure rules are in quick_bar_rules.h.
//
// The entries are rebuilt from live services every frame (data-driven: only what the ship actually has shows up):
//   weapons      combat::ICombat          one Instant entry per weapon slot that has an input action (weapon_1..weapon_3)
//   WARP         ship::IWarpDrive         Toggle, greyed without fuel (same rule as warp_rules.h canEngage)
//   ORBIT LOCK   ship::IOrbitGuide        Toggle, greyed with no body in guide range (docked / warping / dead included)
//   ORE / ANOMALY SCANNER  gameplay::IInventory perks - shown only once installed, always greyed: both are PASSIVE today (nothing to "use")
//
// Activating an entry does not call into the target module: it injects that module's own input action for this frame
// (IInput::contribute right after the input poll), so the bar is just another front door to the same code path as the dedicated key -
// warp's fuel check, orbit lock's alignment refusal, the weapon-changed event, logging, all unchanged.
#include <algorithm>
#include <cctype>
#include <cmath>
#include "combat/weapons/weapons_api.h"
#include "core/audio/audio_api.h"
#include "core/input_handler/input_api.h"
#include "core/ui_handler/ui_handler.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "gameplay/inventory/inventory_api.h"
#include "ship/orbit_lock/orbit_lock_api.h"
#include "ship/ship_core/ship_api.h"
#include "ship/warp_drive/warp_api.h"
#include "ui/controller_setup/controller_setup_api.h"
#include "ui/game_menu/game_menu_api.h"
#include "ui/quick_action_bar/quick_bar_rules.h"

class QuickActionBar : public engine::Module {
public:
    const char* name() const override { return "ui/quick_action_bar"; }
    std::vector<std::string> dependencies() const override { return {"core/input_handler", "core/ui_handler"}; }
    // every source of entries is optional and looked up per frame; these only make init order deterministic when they are loaded
    std::vector<std::string> optionalDependencies() const override {
        return {"combat/weapons", "ship/warp_drive", "ship/orbit_lock", "gameplay/inventory", "ship/ship_core", "ship/fake_ship", "core/audio"};
    }

    bool init(engine::Engine& eng) override {
        eng_ = &eng;
        input_ = &eng.services.require<core::IInput>();
        ui_ = &eng.services.require<core::UIHandler>();
        auto& c = eng.config;
        cfg_.timeout = c.get("quick_bar.timeout", 5.0f, "Quick action bar (B): idle seconds before it closes itself; 0 = stays open until closed");
        cfg_.holdToOpen = c.get("quick_bar.hold_to_open", false, "Quick action bar: true = open only while the open key is held (closes on release); false = press to open, press again to close");
        cfg_.closeOnUse = c.get("quick_bar.close_on_use", false, "Quick action bar: close right after an action fires");
        toggleCooldown_ = std::max(0.0f, c.get("quick_bar.toggle_cooldown", 0.5f, "Quick action bar: seconds a toggle (warp, orbit lock) is locked after use, so a double tap cannot flip it straight back"));
        warpMinFuel_ = c.get("warp.min_fuel", 1.0f, "fuel needed to engage the drive");   // same key as ship/warp_drive: the WARP entry greys out exactly when Z would refuse

        eng.events.subscribe<ship::OrbitLockChanged>([this](const ship::OrbitLockChanged& e) { orbitLocked_ = e.locked; orbitBody_ = e.bodyName; });

        ui_->addPanel("ui/quick_action_bar", 850, [this](core::UIHandler& ui) { if (st_.open && !eng_->paused()) draw(ui); });   // under the game menu (900) and pause menu (1000)
        return true;
    }

    void shutdown(engine::Engine&) override { ui_->removePanel("ui/quick_action_bar"); }

    // Input runs here, right after core/input_handler polled (it inits first), so an activation can inject the target's action into THIS
    // frame and every module's onUpdate sees it as a fresh press.
    void onFrameBegin(engine::Engine& eng) override {
        if (eng.paused() || otherScreenOpen()) {          // the pause menu / game menu / controller screen owns the keys: stand down
            if (st_.open) quickbar::close(st_);
            return;
        }
        quickbar::Input in;
        in.openPressed = input_->pressed("quick_bar_open");
        if (!st_.open && !in.openPressed) return;          // closed and not opening: no need to query every service each frame
        rebuild(eng);
        in.openReleased = input_->released("quick_bar_open");
        in.closePressed = input_->pressed("quick_bar_close");
        in.prev = input_->pressed("quick_bar_prev");
        in.next = input_->pressed("quick_bar_next");
        in.confirm = input_->pressed("quick_bar_confirm");
        auto r = quickbar::step(st_, entries_, in, cfg_);
        switch (r.outcome) {
            case quickbar::Outcome::Opened:    sound("ui_click", 0.5f); break;
            case quickbar::Outcome::Moved:     sound("ui_click", 0.4f); break;
            case quickbar::Outcome::Refused:   sound("ui_click", 0.2f); break;
            case quickbar::Outcome::Activated: activate(r.activated); break;
            default: break;
        }
    }

    void onUpdate(engine::Engine&, float dt) override {
        if (quickbar::tick(st_, dt, cfg_)) LOG_D("quick_bar", "closed (idle %.1f s)", cfg_.timeout);
    }

private:
    bool otherScreenOpen() const {
        if (auto* m = eng_->services.get<ui::IGameMenu>()) if (m->isOpen()) return true;
        if (auto* cs = eng_->services.get<ui::IControllerSetup>()) if (cs->isOpen()) return true;
        return false;
    }
    void sound(const char* n, float vol) { if (auto* a = eng_->services.get<core::IAudio>()) if (a->hasSound(n)) a->play(n, vol); }

    static std::string upper(std::string s) { for (auto& ch : s) ch = (char)std::toupper((unsigned char)ch); return s; }

    // ---- the data-driven entry list, from whatever services exist this frame ----
    void rebuild(engine::Engine& eng) {
        entries_.clear();
        actions_.clear();
        auto add = [&](quickbar::Entry e, std::string action) { actions_[e.id] = std::move(action); entries_.push_back(std::move(e)); };

        if (auto* c = eng.services.get<combat::ICombat>()) {
            int n = c->weaponCount();
            for (int i = 0; i < n; i++) {
                std::string action = quickbar::weaponAction(i);
                if (action.empty()) break;                   // only slots that have an input action to inject
                quickbar::Entry e;
                e.id = action;
                e.label = upper(c->weaponName(i));
                e.kind = quickbar::Kind::Instant;
                e.on = c->selected() == i;
                int ammo = c->ammo(i);
                e.detail = ammo >= 0 ? "x" + std::to_string(ammo) : std::string();
                if (e.on) e.detail = e.detail.empty() ? "SELECTED" : "SELECTED  " + e.detail;
                add(std::move(e), action);
            }
        }

        auto* ship = eng.services.get<ship::IShip>();
        if (auto* warp = eng.services.get<ship::IWarpDrive>()) {
            quickbar::Entry e;
            e.id = "warp"; e.label = "WARP"; e.kind = quickbar::Kind::Toggle; e.cooldown = toggleCooldown_;
            e.on = warp->engaged();
            bool fuelled = ship && ship->status().alive && ship->status().warpFuel >= warpMinFuel_;
            e.available = e.on || fuelled;                   // mirrors warp_rules.h canEngage; turning it off always works
            if (!e.available) e.detail = !ship ? "no ship" : !ship->status().alive ? "ship destroyed" : "no fuel";
            add(std::move(e), "toggle_warp");
        }

        if (auto* guide = eng.services.get<ship::IOrbitGuide>()) {
            quickbar::Entry e;
            e.id = "orbit_lock"; e.label = "ORBIT LOCK"; e.kind = quickbar::Kind::Toggle; e.cooldown = toggleCooldown_;
            e.on = orbitLocked_;
            e.available = orbitLocked_ || guide->active();  // the guide is inactive when docked / warping / dead / nothing in range
            if (orbitLocked_) e.detail = orbitBody_;
            else if (!guide->active()) e.detail = "no body in range";
            else e.detail = guide->aligned() ? guide->bodyName() : guide->refuseReason();   // pressing still asks: orbit lock shows the refusal itself
            add(std::move(e), "toggle_orbit_lock");
        }

        // Scanner perks: both are passive (installed = always on; the crafted item's "use" is what installs them), so there is no action to
        // fire. They show up once installed as a greyed, honest status entry rather than a button that pretends to do something.
        if (auto* inv = eng.services.get<gameplay::IInventory>()) {
            static const struct { const char* perk; const char* label; } kPerks[] = {{"ore_scanner", "ORE SCANNER"}, {"anomaly_scanner", "ANOMALY SCANNER"}};
            for (const auto& p : kPerks) {
                if (!inv->hasPerk(p.perk)) continue;
                quickbar::Entry e;
                e.id = p.perk; e.label = p.label; e.kind = quickbar::Kind::Instant;
                e.available = false; e.on = true; e.detail = "PASSIVE - ALWAYS ON";
                add(std::move(e), std::string());
            }
        }
    }

    void activate(const std::string& id) {
        auto it = actions_.find(id);
        if (it == actions_.end() || it->second.empty()) return;
        input_->contribute(it->second, 1.0f);               // a one-frame press of the dedicated action (next frame's poll releases it)
        sound("ui_confirm", 0.6f);
        LOG_I("quick_bar", "%s -> %s", id.c_str(), it->second.c_str());
    }

    // ---- drawing (glass strip, bottom centre, above the HUD's bottom row) ----
    void draw(core::UIHandler& ui) {
        const int n = (int)entries_.size();
        if (n == 0) return;
        const float sc = ui.scale, W = (float)ui.width(), H = (float)ui.height();
        auto fs = [&](int px) { return std::max(9, (int)std::lround(px * sc)); };
        const float pad = 10 * sc, gap = 8 * sc, itemH = 62 * sc, headH = 22 * sc, footH = 20 * sc;
        float itemW = 138 * sc;
        float maxW = W - 32 * sc;
        if (n * itemW + (n - 1) * gap + pad * 2 > maxW) itemW = std::max(60 * sc, (maxW - pad * 2 - (n - 1) * gap) / n);   // many entries / narrow window
        const float totalW = n * itemW + (n - 1) * gap + pad * 2, totalH = headH + itemH + footH + pad * 2;
        const float x0 = (W - totalW) / 2, y0 = H - totalH - 150 * sc;
        ui.roundedRect(x0, y0, totalW, totalH, 12 * sc, {0.02f, 0.03f, 0.06f, 0.70f}, {0.02f, 0.03f, 0.06f, 0.70f});   // dark backing: readable over a bright planet
        ui.glass(x0, y0, totalW, totalH, 1.0f, false, 12 * sc);
        ui.text(x0 + pad, y0 + pad - 2 * sc, "QUICK ACTIONS", fs(12), ui.theme.textDim);
        // the idle timeout, as a thin line under the title that runs down
        ui.rect(x0 + pad, y0 + pad + 15 * sc, (totalW - pad * 2) * quickbar::timeoutFraction(st_, cfg_), 2 * sc,
                ui.theme.accent.r, ui.theme.accent.g, ui.theme.accent.b, 0.5f);

        const core::Color grey{0.42f, 0.45f, 0.50f, 0.8f}, onCol{0.35f, 0.95f, 0.55f, 1.0f}, offCol{0.85f, 0.45f, 0.40f, 0.9f};
        const float iy = y0 + pad + headH;
        for (int i = 0; i < n; i++) {
            const auto& e = entries_[(size_t)i];
            const float ix = x0 + pad + i * (itemW + gap);
            const bool sel = i == st_.selected;
            const bool ready = quickbar::usable(st_, e);
            ui.glass(ix, iy, itemW, itemH, e.available ? 1.0f : 0.5f, sel, 8 * sc);
            if (!e.available) ui.rect(ix, iy, itemW, itemH, 0.0f, 0.0f, 0.0f, 0.35f);   // greyed
            if (sel) {                                          // accent frame on the selection
                const float t = 2 * sc; const auto& a = ui.theme.accent;
                ui.rect(ix, iy, itemW, t, a.r, a.g, a.b, 0.9f); ui.rect(ix, iy + itemH - t, itemW, t, a.r, a.g, a.b, 0.9f);
                ui.rect(ix, iy, t, itemH, a.r, a.g, a.b, 0.9f); ui.rect(ix + itemW - t, iy, t, itemH, a.r, a.g, a.b, 0.9f);
            }
            // state line (top): ON/OFF for toggles (an instant entry's "current" state is in its detail line, e.g. SELECTED)
            if (e.kind == quickbar::Kind::Toggle) ui.textCentered(ix + itemW / 2, iy + 5 * sc, e.on ? "ON" : "OFF", fs(11), e.on ? onCol : offCol);
            const core::Color& labelCol = !ready ? grey : sel ? ui.theme.text : ui.theme.textDim;
            ui.textCentered(ix + itemW / 2, iy + 22 * sc, e.label, fs(14), labelCol);
            if (!e.detail.empty()) ui.textCentered(ix + itemW / 2, iy + 42 * sc, e.detail, fs(10), e.available ? ui.theme.textDim : grey);
            // cooldown: a dark fill that shrinks from the right as it runs down
            float cd = quickbar::cooldownFraction(st_, e);
            if (cd > 0) ui.rect(ix, iy, itemW * cd, itemH, 0.0f, 0.0f, 0.0f, 0.5f);
        }

        std::string hint = key("quick_bar_prev", ",") + " / " + key("quick_bar_next", ".") + ": select     " + key("quick_bar_confirm", "Return") +
                           ": use     " + key("quick_bar_open", "B") + (cfg_.holdToOpen ? ": release to close" : ": close");
        ui.textCentered(W / 2, y0 + totalH - pad - footH + 6 * sc, hint, fs(11), ui.theme.textDim);
    }
    std::string key(const char* action, const char* fallback) const {
        std::string s = input_->primaryBindingLabel(action);
        return s.empty() ? std::string(fallback) : s;
    }

    engine::Engine* eng_ = nullptr;
    core::IInput* input_ = nullptr;
    core::UIHandler* ui_ = nullptr;
    quickbar::Settings cfg_;
    quickbar::State st_;
    std::vector<quickbar::Entry> entries_;
    std::map<std::string, std::string> actions_;   // entry id -> the input action it injects ("" = nothing to fire)
    float toggleCooldown_ = 0.5f, warpMinFuel_ = 1.0f;
    bool orbitLocked_ = false;
    std::string orbitBody_;
};

REGISTER_MODULE(QuickActionBar);
