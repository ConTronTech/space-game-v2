// ui/main_menu - the title screen between the boot screen and gameplay (docs/MAIN_MENU.md).
//   Main:     Continue (newest save on disk) / New Game / Load Game / Settings / Quit
//   Load:     your saves, newest first (same list as the pause menu's)
//   Settings: the pause menu's own Settings page, borrowed through ui::IPauseMenu (no second copy)
//
// How gameplay is gated: every module still initialises exactly as before (the boot world is generated, the ship is
// spawned), but while this menu is open the engine is held PAUSED - the same freeze the pause menu uses, which the
// gameplay modules already respect (no fixed update = no physics / autosave timer / respawn countdown; weapons, docking,
// warp, orbit lock, particles, game_menu all ignore input while paused; the mouse is released). This module re-asserts
// the pause at the start of every frame, before fixed update, so nothing else can unpause the game underneath it.
// ui::IMainMenu::isOpen() tells the pause menu to stay hidden and ignore Esc. An opaque backdrop (UI panel order 990)
// hides the frozen world, HUD, toasts and overlays. Leaving the menu: New Game just unpauses (the boot world IS a new game); Continue / Load call
// ISaveSystem::loadSlot first, exactly like the pause menu's Load page does mid-game.
//
// Automated runs (--frames, --benchmark, --paused, ... see main_menu_rules.h) skip the menu; --main-menu forces it,
// --no-main-menu or main_menu.enabled = false turn it off.
#include <algorithm>
#include <cmath>
#include "core/audio/audio_api.h"
#include "core/input_handler/input_api.h"
#include "core/save_system/save_api.h"
#include "core/ui_handler/ui_handler.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "ui/controller_setup/controller_setup_api.h"
#include "ui/main_menu/main_menu_api.h"
#include "ui/main_menu/main_menu_rules.h"
#include "ui/pause_menu/pause_menu_api.h"

class MainMenu : public engine::Module, public ui::IMainMenu {
public:
    const char* name() const override { return "ui/main_menu"; }
    std::vector<std::string> dependencies() const override { return {"core/input_handler", "core/ui_handler"}; }
    // After these so their services exist when the item list is built (all optional: the menu works without them).
    std::vector<std::string> optionalDependencies() const override {
        return {"core/save_system", "core/audio", "ui/pause_menu", "ui/controller_setup"};
    }

    bool init(engine::Engine& eng) override {
        eng_ = &eng;
        input_ = &eng.services.require<core::IInput>();
        ui_ = &eng.services.require<core::UIHandler>();
        saves_ = eng.services.get<core::ISaveSystem>();
        audio_ = eng.services.get<core::IAudio>();

        const bool enabled = eng.config.get("main_menu.enabled", true,
            "show the main menu (Continue / New Game / Load / Settings / Quit) after the boot screen, before gameplay starts. "
            "Automated runs (--frames, --benchmark, --paused, ...) skip it; --main-menu forces it, --no-main-menu skips it (docs/MAIN_MENU.md)");
        auto present = [&eng](const std::string& f) { return eng.hasFlag(f) || !eng.flagValue(f).empty(); };
        open_ = mainmenu::shouldShow(enabled, present("main-menu"), present("no-main-menu"), mainmenu::anyAutomationFlag(present));

        eng.services.provide<ui::IMainMenu>(this);
        // 990: above the HUD, toast, debugger and game menu (all <= 900), BELOW the pause menu (1000) so the Settings page it
        // lends us draws on top of our backdrop, and below controller setup (1100).
        ui_->addPanel("ui/main_menu", 990, [this](core::UIHandler& ui) { if (open_) draw(ui); });
        if (open_) {
            showMain();
            eng.setPaused(true);                // nothing simulates until the player picks something
            LOG_I("main_menu", "shown - gameplay held paused until New Game / Continue / Load");
        } else {
            // wording matters: smoke.sh fails on any log line containing "failed", "threw" or "skipped"
            LOG_I("main_menu", "not shown (main_menu.enabled off, --no-main-menu, or an automated run): gameplay starts at once");
        }
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        eng.services.withdraw<ui::IMainMenu>();
        ui_->removePanel("ui/main_menu");
    }

    // ---- ui::IMainMenu ----
    bool isOpen() const override { return open_; }

    // Runs before onFixedUpdate every frame: whatever else happened last frame (e.g. the controller-setup screen closing
    // and releasing a pause it thought it owned), the simulation stays frozen while the menu is up.
    // Also snapshots whether a borrowed screen (Settings / Controllers) was up as the frame began: if so this whole frame's
    // input belongs to it - the Esc / Enter / click that closes it must not also hit one of our buttons.
    void onFrameBegin(engine::Engine& eng) override {
        if (open_ && !eng.paused()) eng.setPaused(true);
        blocked_ = open_ && subScreenOpen();
    }

    void onUpdate(engine::Engine& eng, float) override {
        if (!announced_ && !open_) { announced_ = true; eng.events.emit(ui::GameStarted{ui::GameStarted::How::Skipped, ""}); }
        if (!open_) return;
        if (blocked_ || subScreenOpen()) { pendingClick_ = -1; return; }   // Settings / Controllers screens have the input
        // A click recorded while drawing last frame is applied here, before any panel draws: acting on it inside draw()
        // would open the borrowed Settings page in the same frame, where the same click could land on one of ITS rows.
        if (pendingClick_ >= 0) { int c = pendingClick_; pendingClick_ = -1; if (c < itemCount()) activate(c); return; }

        if (input_->pressed("pause")) { if (page_ != Page::Main) showMain(); return; }   // Esc: back (does nothing on the main page)
        int n = itemCount();
        int before = focus_;
        if (input_->pressed("ui_down")) focus_ = mainmenu::moveFocus(focus_, +1, n);
        if (input_->pressed("ui_up"))   focus_ = mainmenu::moveFocus(focus_, -1, n);
        if (focus_ != before) sound("ui_click", 0.5f);
        if (input_->pressed("ui_confirm")) activate(focus_);
    }

private:
    enum class Page { Main, Load };
    static constexpr size_t kMaxSlotsShown = 6;

    ui::IPauseMenu* pauseMenu() const { return eng_->services.get<ui::IPauseMenu>(); }   // looked up per use, like the pause menu does with controller setup
    bool subScreenOpen() const {
        auto* pm = pauseMenu();
        auto* cs = eng_->services.get<ui::IControllerSetup>();
        return (pm && pm->settingsOnlyOpen()) || (cs && cs->isOpen());
    }
    int itemCount() const { return page_ == Page::Main ? (int)items_.size() : (int)slots_.size() + 1; }   // Load: slots..., Back
    void sound(const char* name, float vol) { if (audio_) audio_->play(name, vol); }
    void setStatus(const std::string& s, bool ok) { status_ = s; statusOk_ = ok; }
    core::SlotInfo continueInfo() const {
        for (const auto& s : allSlots_) if (s.name == continueSlot_) return s;
        return core::SlotInfo{continueSlot_, 0};
    }

    void showMain() {
        Page from = page_;
        allSlots_ = saves_ ? saves_->listSlots() : std::vector<core::SlotInfo>{};
        continueSlot_ = saves_ ? mainmenu::continueSlot(saves_->activeSlot(), allSlots_) : std::string();
        items_ = mainmenu::buildItems(saves_ != nullptr, !continueSlot_.empty(), pauseMenu() != nullptr);
        page_ = Page::Main;
        focus_ = 0;
        if (from == Page::Load)
            for (size_t i = 0; i < items_.size(); i++) if (items_[i] == mainmenu::Item::Load) focus_ = (int)i;
        status_.clear();
    }
    void openLoad() {
        slots_ = allSlots_;
        if (slots_.size() > kMaxSlotsShown) slots_.resize(kMaxSlotsShown);
        page_ = Page::Load;
        focus_ = 0;
        status_.clear();
    }

    // Leave the menu into gameplay. Unpausing is the whole "start": the world has been live (frozen) since boot.
    void start(ui::GameStarted::How how, const std::string& slot) {
        open_ = false;
        announced_ = true;
        eng_->setPaused(false);
        LOG_I("main_menu", "game started (%s%s%s)", how == ui::GameStarted::How::NewGame ? "new game" : how == ui::GameStarted::How::Continue ? "continue" : "load",
              slot.empty() ? "" : ": ", slot.c_str());
        eng_->events.emit(ui::GameStarted{how, slot});
    }
    void loadAndStart(const std::string& slot, ui::GameStarted::How how) {
        if (saves_ && saves_->loadSlot(slot)) start(how, slot);
        else setStatus("Could not load that save - see logs/game.log", false);   // loadSlot left the boot world untouched
    }

    void activate(int i) {
        sound("ui_confirm", 0.8f);
        if (page_ == Page::Load) {
            if (i == (int)slots_.size()) showMain();
            else loadAndStart(slots_[(size_t)i].name, ui::GameStarted::How::Load);
            return;
        }
        switch (items_[(size_t)i]) {
            case mainmenu::Item::Continue: loadAndStart(continueSlot_, ui::GameStarted::How::Continue); break;
            case mainmenu::Item::NewGame:  start(ui::GameStarted::How::NewGame, ""); break;
            case mainmenu::Item::Load:     openLoad(); break;
            case mainmenu::Item::Settings: if (auto* pm = pauseMenu()) pm->openSettingsOnly(); break;
            case mainmenu::Item::Quit:     eng_->quit(); break;
        }
    }

    void draw(core::UIHandler& ui) {
        const float W = (float)ui.width(), H = (float)ui.height();
        ui.rect(0, 0, W, H, 0.01f, 0.015f, 0.03f, 1.0f);   // opaque: the frozen boot world and its HUD are not "the game" yet

        const int n = itemCount();
        const bool emptyLoad = page_ == Page::Load && slots_.empty();
        const int rows = n + (emptyLoad ? 1 : 0);
        // Same layout rules as the pause menu (ui.scale: 1.0 at 1280x720; two columns when a page does not fit).
        const float sc = ui.scale;
        auto fs = [&](int px) { return std::max(9, (int)std::lround(px * sc)); };
        const float pw = 400 * sc, itemH = 52 * sc, gap = 10 * sc, pad = 28 * sc, head = 84 * sc, foot = status_.empty() ? 0.0f : 26 * sc, colGap = 16 * sc;
        const float titleH = 90 * sc;                               // the game title above the glass
        const float avail = H - 24 * sc - titleH;
        int perCol = std::max(1, (int)((avail - head - pad - foot + gap) / (itemH + gap)));
        int cols = std::clamp((rows + perCol - 1) / perCol, 1, 2);
        perCol = (rows + cols - 1) / cols;
        float ph = head + perCol * itemH + (perCol - 1) * gap + pad + foot;
        float totalW = cols * pw + (cols - 1) * colGap;
        float px = (W - totalW) / 2, py = std::max(titleH, (H - ph + titleH) / 2);

        ui.textCentered(W / 2, py - titleH + 16 * sc, "SPACE GAME V2", fs(44), ui.theme.accent);
        if (subScreenOpen()) return;                                // the borrowed Settings / Controllers screen draws over the backdrop
        ui.glass(px, py, totalW, ph, 1.0f, false, 18 * sc);

        const bool mainPage = page_ == Page::Main;
        ui.textCentered(W / 2, py + 22 * sc, mainPage ? "MAIN MENU" : "LOAD GAME", fs(28), ui.theme.text);
        std::string hint = !mainPage ? std::string("Esc to go back")
                         : continueSlot_.empty() ? std::string("No saved games yet")
                         : "Continue: " + mainmenu::slotLabel(continueInfo());
        ui.textCentered(W / 2, py + 58 * sc, hint, fs(13), ui.theme.textDim);

        bool moved = ui.mouseX() != lastMx_ || ui.mouseY() != lastMy_;   // hover moves focus only when the mouse really moved
        lastMx_ = ui.mouseX(); lastMy_ = ui.mouseY();

        float iw = pw - pad * 2;
        auto rowY = [&](int r) { return py + head + (r % perCol) * (itemH + gap); };
        auto colX = [&](int r) { return px + (r / perCol) * (pw + colGap) + pad; };
        if (emptyLoad) ui.textCentered(W / 2, rowY(0) + 16 * sc, "No saved games yet", fs(16), ui.theme.textDim);

        for (int i = 0; i < n; i++) {
            int r = i + (emptyLoad ? 1 : 0);
            float iy = rowY(r), ix = colX(r);
            if (moved && ui.hovered(ix, iy, iw, itemH) && focus_ != i) { focus_ = i; sound("ui_click", 0.5f); }
            bool f = focus_ == i;
            std::string label = mainPage ? mainmenu::label(items_[(size_t)i])
                              : i == (int)slots_.size() ? std::string("Back") : mainmenu::slotLabel(slots_[(size_t)i]);
            if (ui.button(label, ix, iy, iw, itemH, f) && !blocked_) pendingClick_ = i;   // applied in the next onUpdate
        }

        if (!status_.empty())
            ui.textCentered(W / 2, py + ph - pad - 14 * sc, status_, fs(15), statusOk_ ? ui.theme.accent : core::Color{1.0f, 0.45f, 0.4f, 1.0f});
    }

    engine::Engine* eng_ = nullptr;
    core::IInput* input_ = nullptr;
    core::UIHandler* ui_ = nullptr;
    core::ISaveSystem* saves_ = nullptr;
    core::IAudio* audio_ = nullptr;
    std::vector<mainmenu::Item> items_;
    std::vector<core::SlotInfo> allSlots_, slots_;
    std::string continueSlot_, status_;
    Page page_ = Page::Main;
    bool open_ = false, announced_ = false, statusOk_ = true;
    bool blocked_ = false;              // a borrowed screen was open when this frame began (see onFrameBegin)
    int focus_ = 0, lastMx_ = -1, lastMy_ = -1, pendingClick_ = -1;
};

REGISTER_MODULE(MainMenu);
