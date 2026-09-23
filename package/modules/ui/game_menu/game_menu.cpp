// ui/game_menu - a tabbed overlay panel on the I key (and the CARGO tab). Other modules add tabs through ui::IGameMenu. See docs/GAME_MENU.md.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include "core/data_registry/data_api.h"
#include "core/input_handler/input_api.h"
#include "core/ui_handler/ui_handler.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "gameplay/crafting/crafting_api.h"
#include "gameplay/inventory/inventory_api.h"
#include "ship/ship_core/ship_api.h"
#include "ui/game_menu/game_menu_api.h"
#include "ui/game_menu/menu_rules.h"

class GameMenu : public engine::Module, public ui::IGameMenu {
public:
    const char* name() const override { return "ui/game_menu"; }
    std::vector<std::string> dependencies() const override { return {"core/ui_handler", "core/input_handler"}; }
    std::vector<std::string> optionalDependencies() const override {
        return {"core/data_registry", "gameplay/inventory", "ship/ship_core", "ship/fake_ship", "core/audio"};   // ICrafting is looked up per use (crafting itself waits for this module: no cycle)
    }

    bool init(engine::Engine& eng) override {
        eng_ = &eng;
        input_ = &eng.services.require<core::IInput>();
        ui_ = &eng.services.require<core::UIHandler>();
        pauseGame_ = eng.config.get("menu.pause_game", false, "true = the game pauses while the game menu is open (default: the ship keeps flying)");
        std::string of = eng.flagValue("open-menu");
        if (!of.empty()) { openFrame_ = std::atol(of.c_str()); auto c = of.find(','); if (c != std::string::npos) openTab_ = std::atoi(of.c_str() + c + 1); }
        addTab("CARGO", 10, [this](core::UIHandler& ui, float x, float y, float w, float h) { cargoTab(ui, x, y, w, h); });
        ui_->addPanel("ui/game_menu", 900, [this](core::UIHandler& ui) { if (open_) draw(ui); });   // below the pause menu (1000)
        eng.services.provide<ui::IGameMenu>(this);
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        if (open_) close();
        ui_->removePanel("ui/game_menu");
        eng.services.withdraw<ui::IGameMenu>();
    }

    // Esc while the menu is open closes the menu and must NOT also open the pause menu: the pause menu reads the `pause` action in its own onUpdate,
    // so right after the input handler has polled (this module inits after it) the press is swallowed for that frame with IInput::consume(), and
    // kept swallowed while the key is held (otherwise the release/press edge would show up one frame later). consume() hard-zeroes the action for
    // the rest of this frame regardless of how many sources contributed to it (a `contribute("pause", -1)` guess could fail to fully cancel a press
    // that two devices both triggered the same frame; consume() cannot).
    void onFrameBegin(engine::Engine&) override {
        bool close = false;
        if (swallow_.step(open_, input_->value("pause"), close)) input_->consume("pause");
        if (close) closeRequested_ = true;
    }

    void onUpdate(engine::Engine& eng, float) override {
        long f = (long)eng.frame();
        if (openFrame_ >= 0 && f == openFrame_) { open(); selected_ = core::clampTab(openTab_, (int)tabs_.size()); }
        bool toggle = input_->pressed("toggle_menu");
        if (closeRequested_) { closeRequested_ = false; if (open_) close(); return; }
        if (toggle) { if (open_) close(); else open(); return; }
        if (!open_) return;
        // Dedicated tab keys, NOT ui_left/ui_right: this menu does not pause flight (menu.pause_game is off by
        // default), and ui_left/ui_right share A/D with strafe - reusing them here would strafe the ship while
        // switching tabs (docs/QUESTIONS.md #12, fixed 2026-09-22).
        if (input_->pressed("menu_tab_prev")) selected_ = core::cycleTab(selected_, (int)tabs_.size(), -1);
        if (input_->pressed("menu_tab_next")) selected_ = core::cycleTab(selected_, (int)tabs_.size(), +1);
    }

    // ---- ui::IGameMenu ----
    void addTab(const std::string& name, int order, ui::TabDraw draw) override {
        for (auto& t : tabs_) if (t.name == name) { t.order = order; t.draw = std::move(draw); sortTabs(); return; }
        tabs_.push_back({name, order, std::move(draw)});
        sortTabs();
    }
    void removeTab(const std::string& name) override {
        tabs_.erase(std::remove_if(tabs_.begin(), tabs_.end(), [&](const Tab& t) { return t.name == name; }), tabs_.end());
        selected_ = core::clampTab(selected_, (int)tabs_.size());
    }
    void open() override {
        if (open_) return;
        if (eng_->paused()) return;                                          // the pause menu is up: not on top of it
        open_ = true;
        if (pauseGame_) { eng_->setPaused(true); ownPause_ = true; }
        else eng_->events.emit(engine::PauseChanged{true});                  // frees the mouse through the mouse module's own logic (it remembers if it was captured)
        LOG_I("menu", "game menu opened (mouse freed%s)", pauseGame_ ? ", game paused" : "");
    }
    void close() override {
        if (!open_) return;
        open_ = false;
        if (ownPause_) { ownPause_ = false; eng_->setPaused(false); }
        else eng_->events.emit(engine::PauseChanged{false});                 // restores the capture the mouse had before (and skips the first garbage delta)
        LOG_I("menu", "game menu closed (mouse capture restored)");
    }
    bool isOpen() const override { return open_; }

private:
    struct Tab { std::string name; int order; ui::TabDraw draw; };
    void sortTabs() { std::stable_sort(tabs_.begin(), tabs_.end(), [](const Tab& a, const Tab& b) { return a.order < b.order; }); }

    void draw(core::UIHandler& ui) {
        if (tabs_.empty()) return;
        float W = (float)ui.width(), H = (float)ui.height();
        float pw = std::clamp(W * 0.64f, 760.0f, W - 24.0f), ph = std::clamp(H * 0.72f, 480.0f, H - 24.0f);
        float x = (W - pw) / 2, y = (H - ph) / 2;
        ui.roundedRect(x, y, pw, ph, 16, {0.02f, 0.03f, 0.06f, 0.78f}, {0.02f, 0.03f, 0.06f, 0.78f});   // a dark backing: the cockpit model behind must not fight the text
        ui.glass(x, y, pw, ph, 1.0f, false, 16);
        std::vector<std::string> labels;
        for (auto& t : tabs_) labels.push_back(t.name);
        selected_ = ui.tabs(labels, selected_, x + 20, y + 16, pw - 40, 38);
        float cx = x + 20, cy = y + 66, cw = pw - 40, ch = ph - 66 - 34;
        tabs_[(size_t)selected_].draw(ui, cx, cy, cw, ch);
        ui.text(x + 24, y + ph - 26, "I / Esc: close     Left / Right: switch tab", 13, ui.theme.textDim);
    }

    // ---- CARGO tab ----
    struct ItemLook { std::string name; core::Color colour; };
    ItemLook lookOf(const std::string& id) {
        ItemLook l{id, {0.6f, 0.6f, 0.6f, 1}};
        if (id == "rock") { l.name = "Rock"; l.colour = {0.5f, 0.47f, 0.42f, 1}; return l; }
        if (auto* data = eng_->services.get<core::IData>()) {
            for (const char* cat : {"ores", "items"}) {
                if (!data->has(cat, id)) continue;
                const engine::Json& j = data->get(cat, id);
                l.name = j["name"].str(id);
                l.colour = {(float)j["color"].at(0).num(0.6), (float)j["color"].at(1).num(0.6), (float)j["color"].at(2).num(0.6), 1};
                break;
            }
        }
        return l;
    }

    static core::Color fillColour(float frac) {
        return frac < 0.6f ? core::Color{0.3f, 0.85f, 0.45f, 1} : frac < 0.9f ? core::Color{0.95f, 0.8f, 0.25f, 1} : core::Color{0.95f, 0.35f, 0.3f, 1};
    }

    // one row of a hold: colour square, name, a fill bar with "45/100", and the discard buttons "-1" and "ALL" (always possible: the player can never be locked out)
    void holdRow(core::UIHandler& ui, gameplay::IInventory& inv, gameplay::ICrafting* craft, const std::string& id, int amount, float cap, float x, float y, float w, bool withUse) {
        const float rowH = 28;
        ItemLook look = lookOf(id);
        ui.glass(x, y, w, rowH, 0.5f, false, 8);
        ui.rect(x + 8, y + 6, 16, 16, look.colour.r, look.colour.g, look.colour.b, 1.0f);
        ui.text(x + 32, y + 5, look.name, 15, amount > 0 ? ui.theme.text : ui.theme.textDim);
        float btnW = 34 + 4 + 40 + (withUse ? 48.0f : 0.0f);
        float bx = x + 132, bw = std::max(20.0f, w - 132 - btnW - 90);
        float frac = cap > 0 ? std::clamp((float)amount / cap, 0.0f, 1.0f) : 0.0f;
        ui.bar(bx, y + 9, bw, 10, frac, fillColour(frac));
        char b[32]; std::snprintf(b, sizeof b, "%d/%.0f", amount, cap);
        ui.text(bx + bw + 8, y + 6, b, 14, frac >= 0.999f ? core::Color{1.0f, 0.5f, 0.4f, 1} : ui.theme.accent);
        float px = x + w - btnW - 4;
        if (withUse && craft && craft->usable(id) && amount > 0) { if (ui.button("USE", px, y + 1, 44, rowH - 2, false)) { std::string why; craft->use(id, why); } }
        px += withUse ? 48.0f : 0.0f;
        if (amount > 0) {
            if (ui.button("-1", px, y + 1, 34, rowH - 2, false)) discard(inv, id, 1);
            if (ui.button("ALL", px + 38, y + 1, 40, rowH - 2, false)) discard(inv, id, amount);
        }
    }

    void discard(gameplay::IInventory& inv, const std::string& id, int amount) {
        if (amount > 0 && inv.remove(id, amount)) {
            note_ = "Discarded " + std::to_string(amount) + " " + lookOf(id).name;
            noteTime_ = eng_->time();
            LOG_I("menu", "%s", note_.c_str());
        }
    }

    void cargoTab(core::UIHandler& ui, float x, float y, float w, float h) {
        auto* inv = eng_->services.get<gameplay::IInventory>();
        auto* craft = eng_->services.get<gameplay::ICrafting>();
        if (!inv) { ui.text(x, y, "No cargo hold (gameplay/inventory is off)", 16, ui.theme.textDim); return; }
        float leftW = w * 0.55f, rightX = x + leftW + 16, rightW = w - leftW - 16;
        // the summed total: every resource hold plus the general hold
        char buf[64];
        std::snprintf(buf, sizeof buf, "CARGO   %.0f / %.0f", inv->used(), inv->capacity());
        ui.text(x, y, buf, 17, ui.theme.text);
        float frac = inv->capacity() > 0 ? std::clamp(inv->used() / inv->capacity(), 0.0f, 1.0f) : 0.0f;
        ui.bar(x + 190, y + 6, w - 190, 12, frac, fillColour(frac));
        // resources: one hold per ore, so filling one can never block another
        float ry = y + 34;
        ui.text(x, ry, "RESOURCES (each ore has its own hold)", 12, ui.theme.textDim);
        ry += 18;
        std::vector<std::string> ids;
        inv->resources(ids);
        if (inv->count("rock") > 0) ids.push_back("rock");
        for (auto& id : ids) { holdRow(ui, *inv, craft, id, inv->count(id), inv->capacity(id), x, ry, leftW, false); ry += 31; }
        // the general hold: crafted items
        float gy = y + 34;
        char gb[64];
        std::snprintf(gb, sizeof gb, "GENERAL HOLD (items)   %.0f / %.0f", inv->generalUsed(), inv->generalCapacity());
        ui.text(rightX, gy, gb, 12, ui.theme.textDim);
        gy += 18;
        std::vector<gameplay::Stack> st;
        inv->stacks(st);
        int shown = 0;
        bool any = false;
        for (auto& s : st) {
            if (inv->isResource(s.id)) continue;
            any = true;
            if (gy + 28 > y + h - 76) { ui.text(rightX, gy + 4, "...", 14, ui.theme.textDim); break; }
            holdRow(ui, *inv, craft, s.id, s.amount, inv->generalCapacity(), rightX, gy, rightW, true);
            gy += 31; shown++;
        }
        if (!any) ui.text(rightX, gy + 4, "No items. Craft some in the CRAFTING tab.", 13, ui.theme.textDim);
        // ship stats: three compact bars along the bottom
        auto* ship = eng_->services.get<ship::IShip>();
        float sy = y + h - 60;
        if (ship) {
            const auto& s = ship->status();
            float cw = (w - 24) / 3;
            auto stat = [&](int col, const char* label, float v, float mx, const core::Color& c, bool show) {
                float sx = x + col * (cw + 12);
                char b[64]; std::snprintf(b, sizeof b, show ? "%s   %.0f / %.0f" : "%s   not fitted", label, v, mx);
                ui.text(sx, sy, b, 13, show ? ui.theme.text : ui.theme.textDim);
                ui.bar(sx, sy + 20, cw, 10, show && mx > 0 ? std::clamp(v / mx, 0.0f, 1.0f) : 0.0f, c);
            };
            stat(0, "HULL", s.hp, s.maxHp, {0.3f, 0.85f, 0.45f, 1}, true);
            stat(1, "SHIELD", s.shield, s.maxShield, {0.4f, 0.65f, 1.0f, 1}, s.shieldInstalled);
            stat(2, "WARP FUEL", s.warpFuel, s.maxWarpFuel, {0.35f, 0.5f, 1.0f, 1}, true);
        }
        // the last result line: a use / craft message or a discard note, whichever is newer
        bool useMsg = craft && craft->messageAge() < 8.0 && !craft->message().empty();
        bool noteMsg = eng_->time() - noteTime_ < 8.0 && !note_.empty();
        if (noteMsg && (!useMsg || eng_->time() - noteTime_ < craft->messageAge())) ui.text(x, y + h - 22, note_, 14, core::Color{0.8f, 0.9f, 1.0f, 1});
        else if (useMsg) ui.text(x, y + h - 22, craft->message(), 14, craft->messageOk() ? core::Color{0.4f, 0.95f, 0.5f, 1} : core::Color{1.0f, 0.5f, 0.4f, 1});
    }

    engine::Engine* eng_ = nullptr;
    core::IInput* input_ = nullptr;
    core::UIHandler* ui_ = nullptr;
    bool open_ = false, pauseGame_ = false, ownPause_ = false, closeRequested_ = false;
    ui::PauseSwallow swallow_;
    int selected_ = 0, openTab_ = 0;
    std::string note_;
    double noteTime_ = -1e9;
    long openFrame_ = -1;
    std::vector<Tab> tabs_;
};

REGISTER_MODULE(GameMenu);
