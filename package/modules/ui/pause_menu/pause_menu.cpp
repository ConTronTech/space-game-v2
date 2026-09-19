// ui/pause_menu - Esc pauses the simulation and opens a glass menu.
//   Main:     Resume / Settings / Exit Game
//   Settings: Field of View slider, Fullscreen toggle, Back
// Mouse or keyboard/controller (actions: pause, ui_up, ui_down, ui_left, ui_right, ui_confirm).
// Other modules can add pages/items later by copying this shape - it only uses public services.
#include <algorithm>
#include "core/input_handler/input_handler.h"
#include "core/render_engine/render_engine.h"
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
        input_ = &eng.services.require<core::InputHandler>();
        render_ = &eng.services.require<core::RenderEngine>();
        window_ = &eng.services.require<core::Window>();
        ui_ = &eng.services.require<core::UIHandler>();
        ui_->addPanel("ui/pause_menu", 1000, [this](core::UIHandler& ui) { if (eng_->paused()) draw(ui); });
        // dev aid: --paused (or --paused=settings) starts with the menu open
        if (eng.hasFlag("paused") || !eng.flagValue("paused").empty()) eng.setPaused(true);
        if (eng.flagValue("paused") == "settings") page_ = Page::Settings;
        return true;
    }

    void shutdown(engine::Engine&) override { ui_->removePanel("ui/pause_menu"); }

    void onUpdate(engine::Engine& eng, float) override {
        if (input_->pressed("pause")) {
            if (!eng.paused())             { page_ = Page::Main; focus_ = 0; eng.setPaused(true); }
            else if (page_ == Page::Settings) { page_ = Page::Main; focus_ = 1; }
            else                           eng.setPaused(false);
            return;
        }
        if (!eng.paused()) return;

        if (input_->pressed("ui_down")) focus_ = (focus_ + 1) % kItems;
        if (input_->pressed("ui_up"))   focus_ = (focus_ + kItems - 1) % kItems;
        if (input_->pressed("ui_confirm")) activate(focus_);
        if (input_->pressed("ui_left"))  adjust(focus_, -1);
        if (input_->pressed("ui_right")) adjust(focus_, +1);
    }

private:
    enum class Page { Main, Settings };
    static constexpr int kItems = 3;

    void activate(int i) {
        if (page_ == Page::Main) {
            if (i == 0) eng_->setPaused(false);
            else if (i == 1) { page_ = Page::Settings; focus_ = 0; }
            else eng_->quit();
        } else {
            if (i == 1) window_->setFullscreen(!window_->fullscreen());
            else if (i == 2) { page_ = Page::Main; focus_ = 1; }
        }
    }
    void adjust(int i, int dir) {
        if (page_ == Page::Settings && i == 0)
            render_->camera.fovDeg = std::clamp(render_->camera.fovDeg + 5.0f * dir, 60.0f, 120.0f);
    }

    void draw(core::UIHandler& ui) {
        const float W = (float)ui.width(), H = (float)ui.height();
        ui.rect(0, 0, W, H, 0.0f, 0.02f, 0.05f, 0.45f); // dim the game behind the glass

        const float pw = 400, itemH = 52, gap = 10, pad = 28, head = 84;
        float ph = head + kItems * itemH + (kItems - 1) * gap + pad;
        float px = (W - pw) / 2, py = (H - ph) / 2;
        ui.glass(px, py, pw, ph, 1.0f, false, 18);

        bool settings = page_ == Page::Settings;
        ui.textCentered(W / 2, py + 22, settings ? "SETTINGS" : "PAUSED", 28, ui.theme.text);
        ui.textCentered(W / 2, py + 58, settings ? "Esc to go back" : "Esc to resume", 13, ui.theme.textDim);

        // hovering an item moves focus, but only when the mouse actually moved, so keyboard nav still works
        bool moved = ui.mouseX() != lastMx_ || ui.mouseY() != lastMy_;
        lastMx_ = ui.mouseX(); lastMy_ = ui.mouseY();

        float ix = px + pad, iw = pw - pad * 2;
        for (int i = 0; i < kItems; i++) {
            float iy = py + head + i * (itemH + gap);
            if (moved && ui.hovered(ix, iy, iw, itemH)) focus_ = i;
            bool f = focus_ == i;
            if (!settings) {
                static const char* labels[kItems] = {"Resume", "Settings", "Exit Game"};
                if (ui.button(labels[i], ix, iy, iw, itemH, f)) activate(i);
            } else if (i == 0) {
                render_->camera.fovDeg = ui.slider("Field of View", ix, iy, iw, itemH, render_->camera.fovDeg, 60, 120, f);
            } else if (i == 1) {
                bool now = ui.toggle("Fullscreen", ix, iy, iw, itemH, window_->fullscreen(), f);
                if (now != window_->fullscreen()) window_->setFullscreen(now);
            } else if (ui.button("Back", ix, iy, iw, itemH, f)) activate(i);
        }
    }

    engine::Engine* eng_ = nullptr;
    core::InputHandler* input_ = nullptr;
    core::RenderEngine* render_ = nullptr;
    core::Window* window_ = nullptr;
    core::UIHandler* ui_ = nullptr;
    Page page_ = Page::Main;
    int focus_ = 0, lastMx_ = -1, lastMy_ = -1;
};

REGISTER_MODULE(PauseMenu);
