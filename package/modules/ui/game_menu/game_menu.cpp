// ui/game_menu - a tabbed overlay panel on the I key (and the CARGO tab). Other modules add tabs through ui::IGameMenu. See docs/GAME_MENU.md.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
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
        if (selected_ >= 0 && selected_ < (int)tabs_.size() && tabs_[(size_t)selected_].name == "CARGO") cargoInput();   // grid cursor + discard (keyboard / D-pad)
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
        ui.text(x + 24, y + ph - 26, "I / Esc: close     [ / ]: switch tab", 13, ui.theme.textDim);
    }

    // ---- CARGO tab: the unified slot grid (docs/INVENTORY.md "CARGO grid") ----
    // Mouse: press a stack to select it, drag it onto another slot (move / merge / swap) or onto TRASH (discard). Buttons in the side panel.
    // Keyboard / D-pad (wheel, joystick): ui_up/down/left/right move the selection, menu_discard pressed twice on the same slot discards it.
    struct ItemLook { std::string name, kind, desc; core::Color colour; };
    const ItemLook& lookOf(const std::string& id) {
        auto it = looks_.find(id);
        if (it != looks_.end()) return it->second;
        ItemLook l{id, "Item", "", {0.6f, 0.6f, 0.6f, 1}};
        if (id == "rock") { l = {"Rock", "Filler", "Worthless rock scooped up with the ore. Discard it to free the slot.", {0.5f, 0.47f, 0.42f, 1}}; }
        else if (auto* data = eng_->services.get<core::IData>()) {
            for (const char* cat : {"ores", "items"}) {
                if (!data->has(cat, id)) continue;
                const engine::Json& j = data->get(cat, id);
                bool ore = cat[0] == 'o';
                l.name = j["name"].str(id);
                l.kind = ore ? "Ore" : "Item";
                l.desc = j["description"].str(ore ? "Raw ore, mined from asteroids. Used in crafting." : "");
                l.colour = {(float)j["color"].at(0).num(0.6), (float)j["color"].at(1).num(0.6), (float)j["color"].at(2).num(0.6), 1};
                break;
            }
        }
        return looks_.emplace(id, l).first->second;
    }
    std::string levelName(int level) {
        if (auto* data = eng_->services.get<core::IData>()) {
            auto ids = data->ids("cargo");
            if (level >= 0 && level < (int)ids.size()) return data->get("cargo", ids[(size_t)level])["name"].str("Hold");
        }
        return "Hold";
    }

    static core::Color fillColour(float frac) {
        return frac < 0.6f ? core::Color{0.3f, 0.85f, 0.45f, 1} : frac < 0.9f ? core::Color{0.95f, 0.8f, 0.25f, 1} : core::Color{0.95f, 0.35f, 0.3f, 1};
    }

    void discardSlot(gameplay::IInventory& inv, int slot, int amount) {
        if (slot < 0 || slot >= (int)slots_.size() || slots_[(size_t)slot].id.empty()) return;
        std::string id = slots_[(size_t)slot].id;
        int n = inv.discardSlot(slot, amount);
        if (n > 0) {
            note_ = "Discarded " + std::to_string(n) + " " + lookOf(id).name;
            noteTime_ = eng_->time();
            LOG_I("menu", "%s (slot %d)", note_.c_str(), slot);
        }
        inv.slots(slots_);
    }

    // Keyboard / D-pad part of the grid (called from onUpdate while the CARGO tab is showing).
    void cargoInput() {
        auto* inv = eng_->services.get<gameplay::IInventory>();
        if (!inv) return;
        int n = inv->slotCount(), cols = std::max(1, inv->gridColumns());
        int dx = (input_->pressed("ui_right") ? 1 : 0) - (input_->pressed("ui_left") ? 1 : 0);
        int dy = (input_->pressed("ui_down") ? 1 : 0) - (input_->pressed("ui_up") ? 1 : 0);
        if (dx || dy) { sel_ = ui::gridStep(sel_, dx, dy, cols, n); confirm_.reset(); }
        if (input_->pressed("menu_discard")) {
            inv->slots(slots_);
            if (sel_ < 0 || sel_ >= (int)slots_.size() || slots_[(size_t)sel_].id.empty()) { confirm_.reset(); return; }
            if (confirm_.press(sel_, eng_->time())) discardSlot(*inv, sel_, 0);
        }
    }

    // One stack inside a cell: a tinted square, a 2-letter tag, the amount and a thin fill line (amount / stack cap).
    void drawStack(core::UIHandler& ui, gameplay::IInventory& inv, const gameplay::Stack& s, float cx, float cy, float cs, float alpha) {
        const ItemLook& look = lookOf(s.id);
        ui.rect(cx + 3, cy + 3, cs - 6, cs - 6, look.colour.r, look.colour.g, look.colour.b, 0.45f * alpha);
        int fs = std::max(9, (int)(cs * 0.34f)), fa = std::max(9, (int)(cs * 0.30f));
        ui.text(cx + 5, cy + 3, look.name.substr(0, 2), fs, core::Color{ui.theme.text.r, ui.theme.text.g, ui.theme.text.b, alpha});
        std::string amt = std::to_string(s.amount);
        ui.text(cx + cs - 5 - (float)ui.textWidth(amt, fa), cy + cs - fa * 1.3f - 3, amt, fa, core::Color{ui.theme.accent.r, ui.theme.accent.g, ui.theme.accent.b, alpha});
        int cap = inv.stackCap(s.id);
        float frac = cap > 0 ? std::clamp((float)s.amount / (float)cap, 0.0f, 1.0f) : 1.0f;
        core::Color f = fillColour(frac);
        ui.rect(cx + 3, cy + cs - 5, (cs - 6) * frac, 2, f.r, f.g, f.b, 0.9f * alpha);
    }

    static void outline(core::UIHandler& ui, float x, float y, float s, const core::Color& c) {
        const float t = 2;
        ui.rect(x - 1, y - 1, s + 2, t, c.r, c.g, c.b, c.a); ui.rect(x - 1, y + s - 1, s + 2, t, c.r, c.g, c.b, c.a);
        ui.rect(x - 1, y - 1, t, s + 2, c.r, c.g, c.b, c.a); ui.rect(x + s - 1, y - 1, t, s + 2, c.r, c.g, c.b, c.a);
    }

    // Greedy word wrap; returns the y below the last line.
    static float wrapText(core::UIHandler& ui, float x, float y, float w, const std::string& s, int size, const core::Color& c) {
        std::string line, word;
        auto flush = [&]() { if (!line.empty()) { ui.text(x, y, line, size, c); y += size * 1.35f; line.clear(); } };
        for (size_t i = 0; i <= s.size(); i++) {
            if (i < s.size() && s[i] != ' ') { word += s[i]; continue; }
            if (word.empty()) continue;
            std::string test = line.empty() ? word : line + " " + word;
            if (!line.empty() && ui.textWidth(test, size) > w) { flush(); line = word; } else line = test;
            word.clear();
        }
        flush();
        return y;
    }

    void infoPanel(core::UIHandler& ui, gameplay::IInventory& inv, gameplay::ICrafting* craft, int slot, float x, float y, float w, float h) {
        ui.glass(x, y, w, h, 0.7f, false, 10);
        float ix = x + 12, iy = y + 10, iw = w - 24;
        std::string key = input_->primaryBindingLabel("menu_discard");
        if (key.empty()) key = "Delete";
        if (slot < 0 || slot >= (int)slots_.size() || slots_[(size_t)slot].id.empty()) {
            ui.text(ix, iy, slot >= 0 ? "Empty slot" : "No slot selected", 17, ui.theme.textDim);
            iy += 28;
            iy = wrapText(ui, ix, iy, iw, "Ore and crafted items share this grid. Each slot holds one stack; upgrading the hold makes every stack bigger.", 13, ui.theme.textDim);
            iy += 8;
            iy = wrapText(ui, ix, iy, iw, "Mouse: drag a stack onto another slot to move, merge or swap it, or onto TRASH to discard it.", 13, ui.theme.textDim);
            iy += 8;
            wrapText(ui, ix, iy, iw, "Arrows / D-pad: select a slot. " + key + " twice: discard it.", 13, ui.theme.textDim);
            return;
        }
        const gameplay::Stack& s = slots_[(size_t)slot];
        const ItemLook& look = lookOf(s.id);
        ui.rect(ix, iy + 2, 18, 18, look.colour.r, look.colour.g, look.colour.b, 1.0f);
        ui.text(ix + 26, iy, look.name, 18, ui.theme.text);
        iy += 26;
        ui.text(ix, iy, look.kind, 13, ui.theme.accent);
        iy += 22;
        if (!look.desc.empty()) iy = wrapText(ui, ix, iy, iw, look.desc, 14, ui.theme.text) + 6;
        int cap = inv.stackCap(s.id), total = inv.count(s.id), nslots = 0;
        for (auto& o : slots_) if (o.id == s.id) nslots++;
        char b[96];
        std::snprintf(b, sizeof b, "This slot:  %d / %d", s.amount, cap);
        ui.text(ix, iy, b, 14, ui.theme.text); iy += 20;
        std::snprintf(b, sizeof b, "In cargo:  %d  (%d slot%s)", total, nslots, nslots == 1 ? "" : "s");
        ui.text(ix, iy, b, 14, ui.theme.text); iy += 20;
        if (inv.level() + 1 < inv.levelCount()) std::snprintf(b, sizeof b, "Stack size:  %d  (next hold level: %d)", cap, inv.stackCapAtLevel(s.id, inv.level() + 1));
        else std::snprintf(b, sizeof b, "Stack size:  %d  (largest hold)", cap);
        ui.text(ix, iy, b, 13, ui.theme.textDim); iy += 24;
        double now = eng_->time();
        if (confirm_.armed(slot, now)) ui.text(ix, iy, "Press " + key + " again to discard this stack", 13, core::Color{1.0f, 0.5f, 0.4f, 1});
        else ui.text(ix, iy, key + " twice: discard this stack", 13, ui.theme.textDim);
        // buttons along the bottom of the panel (mouse); discarding is always possible, so a full hold can never lock the player out
        float by = y + h - 36, bx = ix;
        if (craft && craft->usable(s.id)) { if (ui.button("USE", bx, by, 60, 28, false)) { std::string why; craft->use(s.id, why); inv.slots(slots_); } bx += 66; }
        if (ui.button("DISCARD 1", bx, by, 96, 28, false)) discardSlot(inv, slot, 1);
        bx += 102;
        if (bx + 130 <= x + w - 8 && ui.button("DISCARD STACK", bx, by, 130, 28, false)) discardSlot(inv, slot, 0);
    }

    void cargoTab(core::UIHandler& ui, float x, float y, float w, float h) {
        auto* inv = eng_->services.get<gameplay::IInventory>();
        auto* craft = eng_->services.get<gameplay::ICrafting>();
        if (!inv) { ui.text(x, y, "No cargo hold (gameplay/inventory is off)", 16, ui.theme.textDim); return; }
        inv->slots(slots_);
        const int n = (int)slots_.size(), cols = std::max(1, inv->gridColumns());
        if (n <= 0) { ui.text(x, y, "The cargo grid has no slots", 16, ui.theme.textDim); return; }
        const int rows = (n + cols - 1) / cols;
        if (sel_ >= n) sel_ = n - 1;
        // mouse edges; the tab was not drawn last frame (another tab / menu closed) = no stale press or release
        long frame = (long)eng_->frame();
        bool held = ui.mouseHeld();
        if (frame != cargoFrame_ + 1) { prevHeld_ = held; dragFrom_ = -1; dragging_ = false; }
        cargoFrame_ = frame;
        bool pressEdge = held && !prevHeld_, releaseEdge = !held && prevHeld_;
        prevHeld_ = held;

        // header: slots used, the hold level and a fill bar
        char buf[128];
        std::snprintf(buf, sizeof buf, "CARGO   %.0f / %.0f slots", inv->used(), inv->capacity());
        ui.text(x, y, buf, 17, ui.theme.text);
        std::snprintf(buf, sizeof buf, "%s  (hold level %d of %d)", levelName(inv->level()).c_str(), inv->level() + 1, inv->levelCount());
        ui.text(x + 260, y + 2, buf, 14, ui.theme.textDim);
        float frac = inv->capacity() > 0 ? std::clamp(inv->used() / inv->capacity(), 0.0f, 1.0f) : 0.0f;

        // layout: the grid on the left, the info panel and the trash on the right
        float top = y + 38, bottom = y + h - 70;
        float pitch = std::floor(std::min((bottom - top) / (float)rows, w * 0.56f / (float)cols));
        pitch = std::max(pitch, 14.0f);
        float gap = std::max(2.0f, std::floor(pitch * 0.08f)), cs = pitch - gap;
        float gx = x, gy = top, gw = cols * pitch;
        ui.bar(x, y + 26, gw - gap, 6, frac, fillColour(frac));
        float px = gx + gw + 14, pw = x + w - px;
        const float trashH = 54;
        float tx = px, ty = gy + rows * pitch - gap - trashH;

        int hover = -1;
        if (ui.hovered(gx, gy, gw, rows * pitch)) {
            int c = (int)((ui.mouseX() - gx) / pitch), r = (int)((ui.mouseY() - gy) / pitch);
            if (c >= 0 && c < cols && r >= 0 && r < rows && r * cols + c < n) hover = r * cols + c;
        }
        bool overTrash = ui.hovered(tx, ty, pw, trashH);
        // mouse: a press selects (and may start a drag); a release on TRASH discards, on another slot moves / merges / swaps
        if (pressEdge && hover >= 0) {
            sel_ = hover; confirm_.reset();
            if (!slots_[(size_t)hover].id.empty()) { dragFrom_ = hover; pressX_ = ui.mouseX(); pressY_ = ui.mouseY(); }
        }
        if (held && dragFrom_ >= 0 && !dragging_ && std::abs(ui.mouseX() - pressX_) + std::abs(ui.mouseY() - pressY_) > 6) dragging_ = true;
        if (releaseEdge) {
            if (dragging_ && dragFrom_ >= 0 && dragFrom_ < n) {
                if (overTrash) discardSlot(*inv, dragFrom_, 0);
                else if (hover >= 0 && hover != dragFrom_ && inv->moveSlot(dragFrom_, hover)) { sel_ = hover; inv->slots(slots_); }
            }
            dragFrom_ = -1; dragging_ = false;
        }
        if (!held && !releaseEdge) { dragFrom_ = -1; dragging_ = false; }

        // the grid
        for (int i = 0; i < n; i++) {
            float cx = gx + (float)(i % cols) * pitch, cy = gy + (float)(i / cols) * pitch;
            const auto& s = slots_[(size_t)i];
            bool drop = dragging_ && i == hover && i != dragFrom_;
            ui.glass(cx, cy, cs, cs, s.id.empty() ? 0.3f : 0.55f, drop, 5);
            if (!s.id.empty()) drawStack(ui, *inv, s, cx, cy, cs, dragging_ && i == dragFrom_ ? 0.35f : 1.0f);
        }
        if (sel_ >= 0 && sel_ < n) {
            bool armed = confirm_.armed(sel_, eng_->time());
            outline(ui, gx + (float)(sel_ % cols) * pitch, gy + (float)(sel_ / cols) * pitch, cs, armed ? core::Color{1.0f, 0.4f, 0.3f, 1} : ui.theme.accent);
        }

        // side panel: the hovered stack (a tooltip) or else the selected slot
        int show = (!dragging_ && hover >= 0 && !slots_[(size_t)hover].id.empty()) ? hover : sel_;
        infoPanel(ui, *inv, craft, show, px, gy, pw, ty - gy - 10);
        // the trash
        ui.glass(tx, ty, pw, trashH, dragging_ ? 1.0f : 0.55f, dragging_ && overTrash, 8);
        ui.textCentered(tx + pw / 2, ty + 8, "TRASH", 16, dragging_ && overTrash ? core::Color{1.0f, 0.5f, 0.4f, 1} : ui.theme.text);
        ui.textCentered(tx + pw / 2, ty + 30, dragging_ ? (overTrash ? "release to discard the stack" : "drop here to discard") : "drag a stack here to discard it", 12, ui.theme.textDim);
        if (dragging_ && dragFrom_ >= 0 && dragFrom_ < n) drawStack(ui, *inv, slots_[(size_t)dragFrom_], ui.mouseX() - cs / 2, ui.mouseY() - cs / 2, cs, 0.85f);

        // ship stats: three compact bars along the bottom
        auto* ship = eng_->services.get<ship::IShip>();
        float sy = y + h - 60;
        if (ship) {
            const auto& st = ship->status();
            float cw = (w - 24) / 3;
            auto stat = [&](int col, const char* label, float v, float mx, const core::Color& c, bool showIt) {
                float sx = x + col * (cw + 12);
                char b[64]; std::snprintf(b, sizeof b, showIt ? "%s   %.0f / %.0f" : "%s   not fitted", label, v, mx);
                ui.text(sx, sy, b, 13, showIt ? ui.theme.text : ui.theme.textDim);
                ui.bar(sx, sy + 20, cw, 10, showIt && mx > 0 ? std::clamp(v / mx, 0.0f, 1.0f) : 0.0f, c);
            };
            stat(0, "HULL", st.hp, st.maxHp, {0.3f, 0.85f, 0.45f, 1}, true);
            stat(1, "SHIELD", st.shield, st.maxShield, {0.4f, 0.65f, 1.0f, 1}, st.shieldInstalled);
            stat(2, "WARP FUEL", st.warpFuel, st.maxWarpFuel, {0.35f, 0.5f, 1.0f, 1}, true);
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
    // the CARGO grid
    std::vector<gameplay::Stack> slots_;                      // this frame's copy of the grid
    std::unordered_map<std::string, ItemLook> looks_;         // display name / colour / description per id (data does not change at runtime)
    int sel_ = 0, dragFrom_ = -1, pressX_ = 0, pressY_ = 0;
    bool dragging_ = false, prevHeld_ = false;
    long cargoFrame_ = -10;
    ui::DiscardConfirm confirm_;
};

REGISTER_MODULE(GameMenu);
