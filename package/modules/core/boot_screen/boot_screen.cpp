// core/boot_screen - a growing bar AND a text log of what is currently loading, shown while the engine's other modules are
// still starting up (package/engine/engine.cpp's loadModules() replays onFrameBegin/onBootOverlay/onPresent after each
// module - deliberately NOT onRenderUI - so this draws normally during that span through the ModuleLoaded event).
// Fixes/covers three things at once (see the incident this followed, docs/STARTUP.md):
//   1. a slow module (image decoding, mesh generation, ...) no longer leaves the window with zero visual feedback - the
//      user sees real progress, and WHICH module is loading, instead of a seemingly frozen black window.
//   2. loadModules() now pumps SDL events between modules (via Window::onFrameBegin, called generically), so the OS/window
//      manager never goes long enough without a response to flag the app as "not responding" during a slow startup.
//   3. this is a safety indicator, so it draws in onBootOverlay, a hook the startup replay calls INSTEAD OF onRenderUI -
//      real game UI (ship_hud, game_menu, ...) never draws at all while still loading (an earlier version of this let
//      onRenderUI run too, so a UI-owning module that had just loaded started drawing PARTIAL, not-yet-ready game state
//      over the loading screen the moment it registered its panel - worse than the freeze this whole module exists to
//      fix). The loading screen is now the ONLY thing on screen for the entire span it is visible.
// Raw GL (fixed-function) and the cockpit's stand-alone stroke font (stroke_font.h - pure glyph data, no module/lifecycle
// coupling, safe to use this early) - no dependency on core/render_engine, which has not loaded yet at this point
// (priority -100; this module inits at -990, right after core/window).
#include <GL/gl.h>
#include <algorithm>
#include <cstdio>
#include <vector>
#include "core/boot_screen/boot_screen_rules.h"
#include "core/window/window.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "ship/cockpit/stroke_font.h"

namespace core {

class BootScreen : public engine::Module {
public:
    const char* name() const override { return "core/boot_screen"; }
    int priority() const override { return -990; }   // right after core/window (-1000), before everything else
    std::vector<std::string> dependencies() const override { return {"core/window"}; }

    bool init(engine::Engine& eng) override {
        if (!eng.config.get("boot_screen.enabled", true, "show a progress bar and the current module while the rest of the engine's modules load (docs/STARTUP.md)")) return true;
        win_ = eng.services.get<Window>();
        if (!win_) return true;   // no window (headless test build) - nothing to draw
        eng.events.subscribe<engine::ModuleLoaded>([this](const engine::ModuleLoaded& e) {
            index_ = e.index; total_ = e.total; label_ = e.name;
            if (e.index >= e.total) reachedTotal_ = true;
        });
        return true;
    }

    // Only ever called during loadModules()'s startup replay (deliberately not onRenderUI - see the file header: game UI
    // must not draw at all while loading is in progress). boot::nextDraw (unit-tested) guarantees the 100%-full frame is
    // drawn exactly once, on the last module's own replay iteration, before this goes silent for the rest of the run.
    void onBootOverlay(engine::Engine&) override {
        boot::DrawDecision d = boot::nextDraw(done_, reachedTotal_);
        done_ = d.stopAfter;
        if (!d.draw || !win_) return;
        boot::Rect track = boot::trackRect(win_->width(), win_->height());
        if (track.w <= 0 || track.h <= 0) return;
        boot::Rect fill = boot::fillRect(track, boot::progressFraction(index_, total_));
        const float textHeight = track.h * 1.3f;

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

        boot::Rect lbl = boot::labelPos(track, textHeight);
        char line[160];
        std::snprintf(line, sizeof line, "LOADING %s (%d/%d)", label_.c_str(), index_, total_);
        drawText(line, lbl.x, lbl.y, textHeight, 0.85f, 0.90f, 0.98f);
        // a short scrolling log of the last few modules, so the user can see real progress (not just one changing name)
        // above the current line - oldest at the top, newest just above the bar. Spaced by the LOG lines' own (smaller)
        // height, with one full extra gap between the log and the current line, so nothing crowds the line above it.
        const float logHeight = textHeight * 0.8f, logStep = logHeight * 1.5f;
        for (size_t i = 0; i < log_.size(); i++)
            drawText(log_[i], lbl.x, lbl.y - textHeight * 1.6f - logStep * (float)(log_.size() - 1 - i), logHeight, 0.55f, 0.58f, 0.62f);

        glPopAttrib();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();

        if (label_ != lastLogged_) { pushLog(label_); lastLogged_ = label_; }
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

    // The cockpit's stroke font (a few line segments per glyph, no texture/font-file dependency) drawn as thick quads -
    // the exact technique ship/cockpit/screen_canvas.h's strip() uses, just in screen pixels instead of "screen heights".
    static void drawText(const std::string& s, float x, float y, float height, float red, float g, float b) {
        static thread_local std::vector<cockpit::Stroke> strokes;
        strokes.clear();
        cockpit::layoutText(s, x, y, height, strokes);
        glColor4f(red, g, b, 1.0f);
        const float t = height * 0.16f;
        glBegin(GL_QUADS);
        for (auto& k : strokes) {
            float dx = k.x2 - k.x1, dy = k.y2 - k.y1, len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-6f) { dx = 1; dy = 0; len = 1; }
            dx /= len; dy /= len;
            float h = t * 0.5f, px = -dy * h, py = dx * h;
            float x1 = k.x1 - dx * h, y1 = k.y1 - dy * h, x2 = k.x2 + dx * h, y2 = k.y2 + dy * h;
            glVertex2f(x1 + px, y1 + py); glVertex2f(x1 - px, y1 - py); glVertex2f(x2 - px, y2 - py); glVertex2f(x2 + px, y2 + py);
        }
        glEnd();
    }

    void pushLog(const std::string& s) {
        log_.push_back(s);
        if (log_.size() > kLogLines) log_.erase(log_.begin());
    }

    static constexpr size_t kLogLines = 5;
    Window* win_ = nullptr;
    int index_ = 0, total_ = 1;
    std::string label_, lastLogged_;
    std::vector<std::string> log_;
    bool done_ = false, reachedTotal_ = false;
};

REGISTER_MODULE(BootScreen);

} // namespace core
