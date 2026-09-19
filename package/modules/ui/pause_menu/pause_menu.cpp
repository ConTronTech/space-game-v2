// ui/pause_menu - Esc pauses the simulation and opens a glass menu.
//   Main:     Resume / Settings / Exit Game
//   Settings: Field of View, Mouse Sensitivity, Fullscreen, Back
// Mouse or keyboard/controller (actions: pause, ui_up, ui_down, ui_left, ui_right, ui_confirm).
// Settings are written through core::ISettings; the module that owns each setting applies it.
#include <algorithm>
#include "core/input_handler/input_api.h"
#include "core/render_engine/render_engine.h"
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
        settings_ = eng.services.get<core::ISettings>();   // optional: without it the settings page is read-only
        ui_->addPanel("ui/pause_menu", 1000, [this](core::UIHandler& ui) { if (eng_->paused()) draw(ui); });
        // dev aid: --paused (or --paused=settings) starts with the menu open
        if (eng.hasFlag("paused") || !eng.flagValue("paused").empty()) eng.setPaused(true);
        if (eng.flagValue("paused") == "settings") page_ = Page::Settings;
        return true;
    }

    void shutdown(engine::Engine&) override { ui_->removePanel("ui/pause_menu"); }

    void onUpdate(engine::Engine& eng, float) override {
        if (input_->pressed("pause")) {
            if (!eng.paused())                { page_ = Page::Main; focus_ = 0; eng.setPaused(true); }
            else if (page_ == Page::Settings) { page_ = Page::Main; focus_ = 1; }
            else                              eng.setPaused(false);
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
    enum class Page { Main, Settings };

    // Settings page rows
    enum { kFov = 0, kSens = 1, kFullscreen = 2, kBack = 3 };
    static constexpr float kFovMin = 60, kFovMax = 120, kSensMin = 0.2f, kSensMax = 3.0f;

    int itemCount() const { return page_ == Page::Main ? 3 : 4; }
    float mouseSens() const { return settings_ ? settings_->get("input.mouse_sensitivity", 1.0f) : 1.0f; }

    void setFov(float v) { if (settings_) settings_->set("video.fov", std::clamp(v, kFovMin, kFovMax)); }
    void setSens(float v) { if (settings_) settings_->set("input.mouse_sensitivity", std::clamp(v, kSensMin, kSensMax)); }
    void setFullscreen(bool on) { if (settings_) settings_->set("video.fullscreen", on); }

    void activate(int i) {
        if (page_ == Page::Main) {
            if (i == 0) eng_->setPaused(false);
            else if (i == 1) { page_ = Page::Settings; focus_ = 0; }
            else eng_->quit();
        } else {
            if (i == kFullscreen) setFullscreen(!window_->fullscreen());
            else if (i == kBack) { page_ = Page::Main; focus_ = 1; }
        }
    }
    void adjust(int i, int dir) {
        if (page_ != Page::Settings) return;
        if (i == kFov) setFov(render_->camera.fovDeg + 5.0f * dir);
        else if (i == kSens) setSens(mouseSens() + 0.1f * dir);
    }

    void draw(core::UIHandler& ui) {
        const float W = (float)ui.width(), H = (float)ui.height();
        ui.rect(0, 0, W, H, 0.0f, 0.02f, 0.05f, 0.45f); // dim the game behind the glass

        const int n = itemCount();
        const float pw = 400, itemH = 52, gap = 10, pad = 28, head = 84;
        float ph = head + n * itemH + (n - 1) * gap + pad;
        float px = (W - pw) / 2, py = (H - ph) / 2;
        ui.glass(px, py, pw, ph, 1.0f, false, 18);

        bool settings = page_ == Page::Settings;
        ui.textCentered(W / 2, py + 22, settings ? "SETTINGS" : "PAUSED", 28, ui.theme.text);
        ui.textCentered(W / 2, py + 58, settings ? "Esc to go back" : "Esc to resume", 13, ui.theme.textDim);

        // hovering an item moves focus, but only when the mouse actually moved, so keyboard nav still works
        bool moved = ui.mouseX() != lastMx_ || ui.mouseY() != lastMy_;
        lastMx_ = ui.mouseX(); lastMy_ = ui.mouseY();

        float ix = px + pad, iw = pw - pad * 2;
        for (int i = 0; i < n; i++) {
            float iy = py + head + i * (itemH + gap);
            if (moved && ui.hovered(ix, iy, iw, itemH)) focus_ = i;
            bool f = focus_ == i;
            if (!settings) {
                static const char* labels[3] = {"Resume", "Settings", "Exit Game"};
                if (ui.button(labels[i], ix, iy, iw, itemH, f)) activate(i);
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
    }

    engine::Engine* eng_ = nullptr;
    core::IInput* input_ = nullptr;
    core::RenderEngine* render_ = nullptr;
    core::Window* window_ = nullptr;
    core::UIHandler* ui_ = nullptr;
    core::ISettings* settings_ = nullptr;
    Page page_ = Page::Main;
    int focus_ = 0, lastMx_ = -1, lastMy_ = -1;
};

REGISTER_MODULE(PauseMenu);
