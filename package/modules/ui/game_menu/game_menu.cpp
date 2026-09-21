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
    // so right after the input handler has polled (this module inits after it) the press is cancelled for that frame by adding -1 to the action, and
    // kept cancelled while the key is held (otherwise the release/press edge would show up one frame later).
    void onFrameBegin(engine::Engine&) override {
        bool close = false;
        if (swallow_.step(open_, input_->value("pause"), close)) input_->contribute("pause", -1.0f);
        if (close) closeRequested_ = true;
    }

    void onUpdate(engine::Engine& eng, float) override {
        long f = (long)eng.frame();
        if (openFrame_ >= 0 && f == openFrame_) { open(); selected_ = core::clampTab(openTab_, (int)tabs_.size()); }
        bool toggle = input_->pressed("toggle_menu");
        if (closeRequested_) { closeRequested_ = false; if (open_) close(); return; }
        if (toggle) { if (open_) close(); else open(); return; }
        if (!open_) return;
        if (input_->pressed("ui_left")) selected_ = core::cycleTab(selected_, (int)tabs_.size(), -1);
        if (input_->pressed("ui_right")) selected_ = core::cycleTab(selected_, (int)tabs_.size(), +1);
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

    void cargoTab(core::UIHandler& ui, float x, float y, float w, float h) {
        auto* inv = eng_->services.get<gameplay::IInventory>();
        auto* craft = eng_->services.get<gameplay::ICrafting>();
        float leftW = w * 0.62f, rightX = x + leftW + 20, rightW = w - leftW - 20;
        if (!inv) { ui.text(x, y, "No cargo hold (gameplay/inventory is off)", 16, ui.theme.textDim); return; }
        // capacity bar
        char buf[64];
        std::snprintf(buf, sizeof buf, "CARGO   %.0f / %.0f", inv->used(), inv->capacity());
        ui.text(x, y, buf, 16, ui.theme.text);
        float frac = inv->capacity() > 0 ? std::clamp(inv->used() / inv->capacity(), 0.0f, 1.0f) : 0.0f;
        core::Color fill = frac < 0.6f ? core::Color{0.3f, 0.85f, 0.45f, 1} : frac < 0.9f ? core::Color{0.95f, 0.8f, 0.25f, 1} : core::Color{0.95f, 0.35f, 0.3f, 1};
        ui.bar(x, y + 26, leftW, 14, frac, fill);
        // the stacks
        std::vector<gameplay::Stack> st;
        inv->stacks(st);
        float ry = y + 54;
        const float rowH = 30;
        int maxRows = std::max(1, (int)((h - 54 - 34) / (rowH + 4)));
        if (st.empty()) ui.text(x, ry + 6, "The hold is empty. Mine asteroids (2 + fire) and scoop the ore.", 14, ui.theme.textDim);
        int shown = 0;
        for (auto& s : st) {
            if (shown >= maxRows) { ui.text(x, ry + 4, "...", 14, ui.theme.textDim); break; }
            ItemLook look = lookOf(s.id);
            ui.glass(x, ry, leftW, rowH, 0.5f, false, 8);
            ui.rect(x + 10, ry + 7, 16, 16, look.colour.r, look.colour.g, look.colour.b, 1.0f);
            ui.text(x + 36, ry + 6, look.name, 15, ui.theme.text);
            std::string count = std::to_string(s.amount);
            bool canUse = craft && craft->usable(s.id);
            float countX = x + leftW - 12 - (canUse ? 74.0f : 0.0f) - ui.textWidth(count, 15);
            ui.text(countX, ry + 6, count, 15, ui.theme.accent);
            if (canUse && ui.button("USE", x + leftW - 68, ry + 2, 62, rowH - 4, false)) {
                std::string why;
                craft->use(s.id, why);                                        // the crafting module keeps the result line
            }
            ry += rowH + 4; shown++;
        }
        if (craft && craft->messageAge() < 8.0 && !craft->message().empty())
            ui.text(x, y + h - 24, craft->message(), 14, craft->messageOk() ? core::Color{0.4f, 0.95f, 0.5f, 1} : core::Color{1.0f, 0.5f, 0.4f, 1});
        // ship stats
        ui.text(rightX, y, "SHIP", 16, ui.theme.text);
        auto* ship = eng_->services.get<ship::IShip>();
        if (!ship) { ui.text(rightX, y + 30, "no ship", 14, ui.theme.textDim); return; }
        const auto& s = ship->status();
        float by = y + 32;
        auto stat = [&](const char* label, float v, float mx, const core::Color& c, bool show = true) {
            char b[64]; std::snprintf(b, sizeof b, show ? "%s   %.0f / %.0f" : "%s   not fitted", label, v, mx);
            ui.text(rightX, by, b, 14, show ? ui.theme.text : ui.theme.textDim);
            ui.bar(rightX, by + 22, rightW, 12, show && mx > 0 ? std::clamp(v / mx, 0.0f, 1.0f) : 0.0f, c);
            by += 50;
        };
        stat("HULL", s.hp, s.maxHp, {0.3f, 0.85f, 0.45f, 1});
        stat("SHIELD", s.shield, s.maxShield, {0.4f, 0.65f, 1.0f, 1}, s.shieldInstalled);
        stat("WARP FUEL", s.warpFuel, s.maxWarpFuel, {0.35f, 0.5f, 1.0f, 1});
    }

    engine::Engine* eng_ = nullptr;
    core::IInput* input_ = nullptr;
    core::UIHandler* ui_ = nullptr;
    bool open_ = false, pauseGame_ = false, ownPause_ = false, closeRequested_ = false;
    ui::PauseSwallow swallow_;
    int selected_ = 0, openTab_ = 0;
    long openFrame_ = -1;
    std::vector<Tab> tabs_;
};

REGISTER_MODULE(GameMenu);
