// ui/pause_menu - Esc pauses the simulation and opens a glass menu.
//   Main:     Resume / Save Game / Load Game / Settings / Exit Game   (Save/Load only if core/save_system is loaded)
//   Load:     your saves, newest first
//   Settings: Field of View, Mouse Sensitivity, Fullscreen, Back
// Mouse or keyboard/controller (actions: pause, ui_up, ui_down, ui_left, ui_right, ui_confirm).
// Settings go through core::ISettings; saving/loading goes through core::ISaveSystem.
#include <algorithm>
#include <ctime>
#include "core/input_handler/input_api.h"
#include "core/render_engine/render_engine.h"
#include "core/save_system/save_api.h"
#include "core/settings/settings_api.h"
#include "core/ui_handler/ui_handler.h"
#include "core/window/window.h"
#include "engine/engine.h"

class PauseMenu : public engine::Module {
public:
    const char* name() const override { return "ui/pause_menu"; }
    std::vector<std::string> dependencies() const override {
        return {"core/input_handler", "core/ui_handler", "core/render_engine", "core/window"};
    }

    bool init(engine::Engine& eng) override {
        eng_ = &eng;
        input_ = &eng.services.require<core::IInput>();
        render_ = &eng.services.require<core::RenderEngine>();
        window_ = &eng.services.require<core::Window>();
        ui_ = &eng.services.require<core::UIHandler>();
        settings_ = eng.services.get<core::ISettings>();     // optional: without it the settings page is read-only
        saves_ = eng.services.get<core::ISaveSystem>();      // optional: without it Save/Load are hidden

        main_ = {Main::Resume};
        if (saves_) { main_.push_back(Main::Save); main_.push_back(Main::Load); }
        main_.push_back(Main::Settings);
        main_.push_back(Main::Exit);

        ui_->addPanel("ui/pause_menu", 1000, [this](core::UIHandler& ui) { if (eng_->paused()) draw(ui); });
        // dev aid: --paused (or --paused=settings|load) starts with the menu open
        if (eng.hasFlag("paused") || !eng.flagValue("paused").empty()) eng.setPaused(true);
        if (eng.flagValue("paused") == "settings") page_ = Page::Settings;
        if (eng.flagValue("paused") == "load" && saves_) openLoad();
        return true;
    }

    void shutdown(engine::Engine&) override { ui_->removePanel("ui/pause_menu"); }

    void onUpdate(engine::Engine& eng, float) override {
        if (input_->pressed("pause")) {
            if (!eng.paused())         { page_ = Page::Main; focus_ = 0; status_.clear(); eng.setPaused(true); }
            else if (page_ != Page::Main) backToMain();
            else                       eng.setPaused(false);
            return;
        }
        if (!eng.paused()) return;

        int n = itemCount();
        if (input_->pressed("ui_down")) focus_ = (focus_ + 1) % n;
        if (input_->pressed("ui_up"))   focus_ = (focus_ + n - 1) % n;
        if (input_->pressed("ui_confirm")) activate(focus_);
        if (input_->pressed("ui_left"))  adjust(focus_, -1);
        if (input_->pressed("ui_right")) adjust(focus_, +1);
    }

private:
    enum class Page { Main, Load, Settings };
    enum class Main { Resume, Save, Load, Settings, Exit };
    enum { kFov = 0, kSens = 1, kFullscreen = 2, kBack = 3 };   // settings rows
    static constexpr float kFovMin = 60, kFovMax = 120, kSensMin = 0.2f, kSensMax = 3.0f;
    static constexpr size_t kMaxSlotsShown = 6;

    int itemCount() const {
        switch (page_) {
            case Page::Main: return (int)main_.size();
            case Page::Load: return (int)slots_.size() + 1;      // slots..., Back
            default: return 4;
        }
    }
    int mainIndexOf(Main m) const { for (size_t i = 0; i < main_.size(); i++) if (main_[i] == m) return (int)i; return 0; }
    float mouseSens() const { return settings_ ? settings_->get("input.mouse_sensitivity", 1.0f) : 1.0f; }

    void setFov(float v) { if (settings_) settings_->set("video.fov", std::clamp(v, kFovMin, kFovMax)); }
    void setSens(float v) { if (settings_) settings_->set("input.mouse_sensitivity", std::clamp(v, kSensMin, kSensMax)); }
    void setFullscreen(bool on) { if (settings_) settings_->set("video.fullscreen", on); }

    void backToMain() {
        Page from = page_;
        page_ = Page::Main;
        focus_ = mainIndexOf(from == Page::Load ? Main::Load : Main::Settings);
        status_.clear();
    }
    void openLoad() {
        slots_ = saves_->listSlots();
        if (slots_.size() > kMaxSlotsShown) slots_.resize(kMaxSlotsShown);
        page_ = Page::Load;
        focus_ = 0;
        status_.clear();
    }
    void setStatus(const std::string& s, bool ok) { status_ = s; statusOk_ = ok; }

    void activate(int i) {
        if (page_ == Page::Main) {
            switch (main_[(size_t)i]) {
                case Main::Resume:   eng_->setPaused(false); break;
                case Main::Save: {
                    std::string slot = saves_->newSlotName();
                    if (saves_->saveSlot(slot)) setStatus("Game saved", true);
                    else setStatus("Save failed - see logs/game.log", false);
                    break;
                }
                case Main::Load:     openLoad(); break;
                case Main::Settings: page_ = Page::Settings; focus_ = 0; status_.clear(); break;
                case Main::Exit:     eng_->quit(); break;
            }
        } else if (page_ == Page::Load) {
            if (i == (int)slots_.size()) backToMain();
            else if (saves_->loadSlot(slots_[(size_t)i].name)) eng_->setPaused(false);
            else setStatus("Could not load that save - see logs/game.log", false);
        } else {
            if (i == kFullscreen) setFullscreen(!window_->fullscreen());
            else if (i == kBack) backToMain();
        }
    }
    void adjust(int i, int dir) {
        if (page_ != Page::Settings) return;
        if (i == kFov) setFov(render_->camera.fovDeg + 5.0f * dir);
        else if (i == kSens) setSens(mouseSens() + 0.1f * dir);
    }

    static std::string when(long long t) {
        std::time_t tt = (std::time_t)t;
        char b[32];
        std::strftime(b, sizeof b, "%Y-%m-%d  %H:%M:%S", std::localtime(&tt));
        return b;
    }

    void draw(core::UIHandler& ui) {
        const float W = (float)ui.width(), H = (float)ui.height();
        ui.rect(0, 0, W, H, 0.0f, 0.02f, 0.05f, 0.45f); // dim the game behind the glass

        const int n = itemCount();
        const bool emptyLoad = page_ == Page::Load && slots_.empty();
        const int rows = n + (emptyLoad ? 1 : 0);           // an extra text row explains an empty list
        const float pw = 400, itemH = 52, gap = 10, pad = 28, head = 84, foot = status_.empty() ? 0.0f : 26.0f;
        float ph = head + rows * itemH + (rows - 1) * gap + pad + foot;
        float px = (W - pw) / 2, py = (H - ph) / 2;
        ui.glass(px, py, pw, ph, 1.0f, false, 18);

        const char* title = page_ == Page::Main ? "PAUSED" : page_ == Page::Load ? "LOAD GAME" : "SETTINGS";
        ui.textCentered(W / 2, py + 22, title, 28, ui.theme.text);
        ui.textCentered(W / 2, py + 58, page_ == Page::Main ? "Esc to resume" : "Esc to go back", 13, ui.theme.textDim);

        // hovering an item moves focus, but only when the mouse actually moved, so keyboard nav still works
        bool moved = ui.mouseX() != lastMx_ || ui.mouseY() != lastMy_;
        lastMx_ = ui.mouseX(); lastMy_ = ui.mouseY();

        float ix = px + pad, iw = pw - pad * 2;
        auto rowY = [&](int r) { return py + head + r * (itemH + gap); };
        if (emptyLoad) ui.textCentered(W / 2, rowY(0) + 16, "No saved games yet", 16, ui.theme.textDim);

        for (int i = 0; i < n; i++) {
            float iy = rowY(i + (emptyLoad ? 1 : 0));
            if (moved && ui.hovered(ix, iy, iw, itemH)) focus_ = i;
            bool f = focus_ == i;

            if (page_ == Page::Main) {
                static const char* labels[] = {"Resume", "Save Game", "Load Game", "Settings", "Exit Game"};
                if (ui.button(labels[(int)main_[(size_t)i]], ix, iy, iw, itemH, f)) activate(i);
            } else if (page_ == Page::Load) {
                if (i == (int)slots_.size()) { if (ui.button("Back", ix, iy, iw, itemH, f)) activate(i); }
                else if (ui.button(when(slots_[(size_t)i].time), ix, iy, iw, itemH, f)) activate(i);
            } else if (i == kFov) {
                float cur = render_->camera.fovDeg;
                float v = ui.slider("Field of View", ix, iy, iw, itemH, cur, kFovMin, kFovMax, f);
                if (v != cur) setFov(v);
            } else if (i == kSens) {
                float cur = mouseSens();
                float v = ui.slider("Mouse Sensitivity", ix, iy, iw, itemH, cur, kSensMin, kSensMax, f, "%.2f");
                if (v != cur) setSens(v);
            } else if (i == kFullscreen) {
                bool cur = window_->fullscreen();
                bool now = ui.toggle("Fullscreen", ix, iy, iw, itemH, cur, f);
                if (now != cur) setFullscreen(now);
            } else if (ui.button("Back", ix, iy, iw, itemH, f)) activate(i);
        }

        if (!status_.empty())
            ui.textCentered(W / 2, py + ph - pad - 14, status_, 15, statusOk_ ? ui.theme.accent : core::Color{1.0f, 0.45f, 0.4f, 1.0f});
    }

    engine::Engine* eng_ = nullptr;
    core::IInput* input_ = nullptr;
    core::RenderEngine* render_ = nullptr;
    core::Window* window_ = nullptr;
    core::UIHandler* ui_ = nullptr;
    core::ISettings* settings_ = nullptr;
    core::ISaveSystem* saves_ = nullptr;
    std::vector<Main> main_;
    std::vector<core::SlotInfo> slots_;
    Page page_ = Page::Main;
    std::string status_;
    bool statusOk_ = true;
    int focus_ = 0, lastMx_ = -1, lastMy_ = -1;
};

REGISTER_MODULE(PauseMenu);
