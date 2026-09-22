// core/boot_screen - a simple growing bar shown while the engine's other modules are still loading (package/engine/engine.cpp's
// loadModules() replays onFrameBegin/onRenderUI/onPresent after each module, exactly like a real frame, so this draws normally
// during that span through the ModuleLoaded event). Fixes two things at once (see the incident this followed, docs/STARTUP.md):
//   1. a slow module (image decoding, mesh generation, ...) no longer leaves the window with zero visual feedback - the user
//      sees real progress instead of a seemingly frozen black window.
//   2. loadModules() now pumps SDL events between modules (via Window::onFrameBegin, called generically), so the OS/window
//      manager never goes long enough without a response to flag the app as "not responding" during a slow startup.
// Raw GL (fixed-function, matches core/window's own style) - no dependency on core/render_engine, since this must work even
// if render_engine has not loaded yet (it inits later, priority -100, this module inits at -990, right after core/window).
#include <GL/gl.h>
#include <algorithm>
#include "core/boot_screen/boot_screen_rules.h"
#include "core/window/window.h"
#include "engine/engine.h"
#include "engine/log.h"

namespace core {

class BootScreen : public engine::Module {
public:
    const char* name() const override { return "core/boot_screen"; }
    int priority() const override { return -990; }   // right after core/window (-1000), before everything else
    std::vector<std::string> dependencies() const override { return {"core/window"}; }

    bool init(engine::Engine& eng) override {
        if (!eng.config.get("boot_screen.enabled", true, "show a progress bar while the rest of the engine's modules load (docs/STARTUP.md)")) return true;
        win_ = eng.services.get<Window>();
        if (!win_) return true;   // no window (headless test build) - nothing to draw
        eng.events.subscribe<engine::ModuleLoaded>([this](const engine::ModuleLoaded& e) {
            index_ = e.index; total_ = e.total;
            if (e.index >= e.total) reachedTotal_ = true;
        });
        return true;
    }

    // Called during loadModules()'s replay (while modules are still loading), and once more as a completely normal early
    // frame after loadModules() returns - boot::nextDraw (unit-tested) guarantees the 100%-full frame is drawn exactly
    // once before this goes silent for the rest of the run, at zero cost (an early-return, no GL calls at all).
    void onRenderUI(engine::Engine&) override {
        boot::DrawDecision d = boot::nextDraw(done_, reachedTotal_);
        done_ = d.stopAfter;
        if (!d.draw || !win_) return;
        boot::Rect track = boot::trackRect(win_->width(), win_->height());
        if (track.w <= 0 || track.h <= 0) return;
        boot::Rect fill = boot::fillRect(track, boot::progressFraction(index_, total_));

        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0, win_->width(), win_->height(), 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);

        drawRect(track.x, track.y, track.w, track.h, 0.15f, 0.16f, 0.20f, 0.9f);   // track (dark)
        if (fill.w > 0) drawRect(fill.x, fill.y, fill.w, fill.h, 0.35f, 0.65f, 0.95f, 1.0f);   // fill (bright blue), grows left to right

        glPopAttrib();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
    }

private:
    static void drawRect(float x, float y, float w, float h, float red, float g, float b, float a) {
        glColor4f(red, g, b, a);
        glBegin(GL_QUADS);
        glVertex2f(x, y);
        glVertex2f(x + w, y);
        glVertex2f(x + w, y + h);
        glVertex2f(x, y + h);
        glEnd();
    }

    Window* win_ = nullptr;
    int index_ = 0, total_ = 1;
    bool done_ = false, reachedTotal_ = false;
};

REGISTER_MODULE(BootScreen);

} // namespace core
