// core/profiler_overlay - the LIVE profiler front end: F3 opens a compact panel (fps, frame times, frame graph, top consumers, hitch count),
// F4 (while it is open) switches GPU attribution (glFinish around render passes), F5 writes logs/profile_live.txt. See docs/PERFORMANCE.md.
// The recording itself is engine::Profiler (always on, profiler.lite): this module only reads it. Without core/ui_handler it still handles F5.
#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include "core/input_handler/input_api.h"
#include "core/quality/quality_api.h"
#include "core/render_engine/render_engine.h"
#include "core/ui_handler/ui_handler.h"
#include "core/window/window.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "engine/profiler.h"

namespace core {

class ProfilerOverlay : public engine::Module {
public:
    const char* name() const override { return "core/profiler_overlay"; }
    std::vector<std::string> optionalDependencies() const override {
        return {"core/input_handler", "core/ui_handler", "core/quality", "core/window", "core/render_engine"};
    }

    bool init(engine::Engine& eng) override {
        eng_ = &eng;
        prof_ = eng.services.get<engine::Profiler>();
        if (!prof_ || !prof_->liveEnabled()) { LOG_I("profiler", "live profiler is off (profiler.lite = false)"); prof_ = nullptr; return true; }
        input_ = eng.services.get<IInput>();
        ui_ = eng.services.get<UIHandler>();
        quality_ = eng.services.get<IQuality>();
        window_ = eng.services.get<Window>();
        render_ = eng.services.get<RenderEngine>();
        open_ = eng.hasFlag("profile-overlay");
        if (!eng.flagValue("profile-dump").empty()) dumpAt_ = std::atol(eng.flagValue("profile-dump").c_str());   // dev aid: press F5 on that frame
        if (!eng.flagValue("profile-gpu-toggle").empty()) gpuToggleAt_ = std::atol(eng.flagValue("profile-gpu-toggle").c_str());   // dev aid: press F4 on that frame
        if (window_) {
            const char* r = (const char*)glGetString(GL_RENDERER);
            const char* v = (const char*)glGetString(GL_VERSION);
            glRenderer_ = r ? r : "?";
            glVersion_ = v ? v : "?";
        }
        eng.events.subscribe<WindowResized>([this](const WindowResized&) { if (prof_) prof_->noteResize(); });   // a resize / fullscreen switch is not a hitch
        if (ui_) ui_->addPanel("core/profiler_overlay", 900, [this](UIHandler& ui) { draw(ui); });
        LOG_I("profiler", "live profiler on: F3 overlay, F4 GPU mode (overlay open), F5 dump to logs/profile_live.txt%s", open_ ? " (overlay open: --profile-overlay)" : "");
        return true;
    }

    void shutdown(engine::Engine&) override {
        if (ui_ && prof_) ui_->removePanel("core/profiler_overlay");
    }

    void onUpdate(engine::Engine& eng, float) override {
        if (!prof_) return;
        if (input_) {
            if (input_->pressed("toggle_profiler")) { open_ = !open_; snapshotDue_ = 0; }
            if (input_->pressed("toggle_profiler_gpu")) {
                if (open_) { prof_->setGpuMode(!prof_->gpuMode()); LOG_I("profiler", "GPU mode %s", prof_->gpuMode() ? "ON (glFinish around passes: slower, ranks passes)" : "off"); }
                else LOG_D("profiler", "F4 ignored: open the overlay first (F3)");
            }
            if (input_->pressed("dump_profile")) dump();
        }
        if (dumpAt_ >= 0 && (long)eng.frame() == dumpAt_) dump();
        if (gpuToggleAt_ >= 0 && (long)eng.frame() == gpuToggleAt_) { prof_->setGpuMode(!prof_->gpuMode()); LOG_I("profiler", "GPU mode %s (--profile-gpu-toggle)", prof_->gpuMode() ? "ON" : "off"); }
        if (open_ && eng.time() >= snapshotDue_) { snapshot(); snapshotDue_ = eng.time() + 0.25; }   // text is rebuilt 4 times a second: the UI text cache keeps the textures
    }

private:
    // ---------- text snapshot (4 Hz) ----------
    struct Row { std::string name, ms; float frac = 0; };
    struct Snap {
        std::string fps, frameNow, frameAvg, low, hitches, info1, info2;
        bool hitchAmber = false;
        std::array<Row, 6> rows;
        int rowCount = 0;
    } snap_;

    static std::string shorten(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 1) + "~"; }

    void snapshot() {
        using namespace engine::live;
        const FrameRing& r = *prof_->ring();
        if (r.size() == 0) return;
        FrameStats s1 = statsOver(r, framesInWindow(r, 1000)), s5 = statsOver(r, framesInWindow(r, 5000)), s10 = statsOver(r, framesInWindow(r, 10000));
        snap_.fps = "FPS " + fmtFps(s1.avgFps);
        snap_.frameNow = "frame " + fmt1(r.frameMsAgo(0)) + " ms";
        snap_.frameAvg = "avg " + fmt1(s5.avgMs) + " / worst " + fmt1(s5.worstMs) + " ms  (" + fmt1(s5.frames * s5.avgMs / 1000.0) + " s)";
        snap_.low = "1% low " + fmtFps(s10.lowFps) + " fps  (" + fmt1(s10.frames * s10.avgMs / 1000.0) + " s)";
        int hitches = prof_->hitches().countSince(prof_->timeSec(), 30.0);
        snap_.hitchAmber = hitches > 0;
        snap_.hitches = hitches > 0 ? std::to_string(hitches) + (hitches == 1 ? " hitch" : " hitches") + " in the last 30 s (> " + fmt1(prof_->hitchMs()) + " ms)"
                                    : "no hitches (> " + fmt1(prof_->hitchMs()) + " ms) in the last 30 s";
        std::array<unsigned char, kMaxSlots> skip{};
        for (int i = 0; i < prof_->slotCount() && i < kMaxSlots; i++) skip[(size_t)i] = prof_->isContainer(i) ? 1 : 0;
        std::array<Consumer, 6> top;
        int frames = framesInWindow(r, 1000);
        snap_.rowCount = topConsumers(r, frames, 6, top.data(), skip.data());
        for (int i = 0; i < snap_.rowCount; i++) {
            snap_.rows[(size_t)i].name = shorten(prof_->slotName(top[(size_t)i].slot), 32);
            snap_.rows[(size_t)i].ms = fmt1(top[(size_t)i].avgMs) + " ms";
            snap_.rows[(size_t)i].frac = s1.avgMs > 0 ? std::min(1.0f, top[(size_t)i].avgMs / (float)s1.avgMs) : 0.0f;
        }
        snap_.info1 = describeSettings();
        snap_.info2 = "GL: " + shorten(glRenderer_, 46);
    }

    std::string describeSettings() const {
        std::string q = quality_ ? quality_->activeName() + (quality_->selectedName() == "auto" ? " (auto)" : "") : "n/a";
        std::string size;
        if (window_) {
            size = std::to_string(window_->width()) + "x" + std::to_string(window_->height()) + (window_->fullscreen() ? " fullscreen" : " windowed");
            if (render_ && render_->worldBufferActive()) {
                char b[64];
                std::snprintf(b, sizeof b, ", world at %.0f%%", render_->renderScale() * 100.0f);
                size += b;
            }
        }
        return "quality " + q + " | " + size;
    }

    // ---------- drawing ----------
    void draw(UIHandler& ui) {
        using namespace engine::live;
        if (!prof_) return;
        if (!open_) { drawToast(ui); return; }
        const FrameRing& r = *prof_->ring();
        const float W = 392, x = (float)ui.width() - W - 12, top = 12;
        const float pad = 12;
        float y = top + 8;
        const float H = 44 + 3 * 17 + 6 + 56 + 22 + 20 + (float)snap_.rowCount * 20 + 40 + (prof_->gpuMode() ? 18 : 0) + (toastActive() ? 18 : 0);
        ui.glass(x, top, W, H, 0.95f, false, 8);
        const Color dim = ui.theme.textDim, txt = ui.theme.text, amber{1.0f, 0.75f, 0.2f, 1.0f};

        ui.text(x + pad, y, "PROFILER", 14, ui.theme.accent);
        ui.text(x + pad + 92, y + 1, "F3 close   F4 GPU mode   F5 dump", 12, dim);
        y += 22;
        ui.text(x + pad, y, snap_.fps, 15, txt);
        ui.text(x + pad + 96, y, snap_.frameNow, 15, txt);
        y += 20;
        ui.text(x + pad, y, snap_.frameAvg, 13, dim);
        y += 17;
        ui.text(x + pad, y, snap_.low, 13, dim);
        y += 22;

        // frame-time graph: the last 120 frames, oldest on the left; one GL batch (never one draw per bar)
        const float gw = 360, gh = 44, barW = gw / 120.0f;
        glBegin(GL_QUADS);
        glColor4f(0.0f, 0.0f, 0.0f, 0.35f);
        glVertex2f(x + pad, y); glVertex2f(x + pad + gw, y); glVertex2f(x + pad + gw, y + gh); glVertex2f(x + pad, y + gh);
        int n = std::min(120, r.size());
        for (int i = 0; i < n; i++) {
            Bar b = barFor(r.frameMsAgo(n - 1 - i));
            float h = std::max(1.0f, b.height * gh);
            float bx = x + pad + (float)(120 - n + i) * barW;
            if (b.color == BarColor::Red) glColor4f(1.0f, 0.25f, 0.2f, 0.95f);
            else if (b.color == BarColor::Amber) glColor4f(1.0f, 0.75f, 0.2f, 0.95f);
            else glColor4f(0.35f, 0.85f, 1.0f, 0.85f);
            glVertex2f(bx, y + gh - h); glVertex2f(bx + barW * 0.8f, y + gh - h); glVertex2f(bx + barW * 0.8f, y + gh); glVertex2f(bx, y + gh);
        }
        for (double line : {kRefLineMs, kRedMs}) {                       // the 16.7 ms and 33 ms lines
            float ly = y + gh - (float)barFor(line).height * gh;
            glColor4f(line == kRedMs ? 1.0f : 1.0f, line == kRedMs ? 0.3f : 0.75f, line == kRedMs ? 0.25f : 0.2f, 0.55f);
            glVertex2f(x + pad, ly); glVertex2f(x + pad + gw, ly); glVertex2f(x + pad + gw, ly + 1); glVertex2f(x + pad, ly + 1);
        }
        glEnd();
        y += gh + 6;

        ui.text(x + pad, y, snap_.hitches, 13, snap_.hitchAmber ? amber : dim);
        y += 22;
        ui.text(x + pad, y, "TOP CONSUMERS (avg per frame, last second)", 12, dim);
        y += 18;
        for (int i = 0; i < snap_.rowCount; i++) {
            const Row& row = snap_.rows[(size_t)i];
            ui.text(x + pad, y, row.name, 12, txt);
            ui.bar(x + pad + 232, y + 3, 96, 8, row.frac, {0.35f, 0.85f, 1.0f, 1.0f});
            ui.text(x + pad + 336, y, row.ms, 12, dim);
            y += 20;
        }
        y += 4;
        ui.text(x + pad, y, snap_.info1, 12, dim);
        y += 16;
        ui.text(x + pad, y, snap_.info2, 12, dim);
        y += 18;
        if (prof_->gpuMode()) { ui.text(x + pad, y, "GPU MODE (slower, ranks passes)", 13, amber); y += 18; }
        if (toastActive()) ui.text(x + pad, y, toast_, 12, ui.theme.accent);
    }

    // a short line when the overlay is closed and a dump was just written
    void drawToast(UIHandler& ui) {
        if (!toastActive()) return;
        float w = (float)ui.textWidth(toast_, 13) + 24;
        ui.glass((float)ui.width() - w - 12, 12, w, 30, 0.95f, false, 8);
        ui.text((float)ui.width() - w, 19, toast_, 13, ui.theme.accent);
    }
    bool toastActive() const { return eng_->time() < toastUntil_; }

    // ---------- the F5 snapshot ----------
    void dump() {
        std::vector<std::string> h;
        char b[256];
        std::snprintf(b, sizeof b, "build:          %s %s (game version: see the git tag of this build)", __DATE__, __TIME__);
        h.push_back(b);
        h.push_back("uptime:         " + engine::live::fmtUptime(eng_->time()) + "  (frame " + std::to_string(eng_->frame()) + ")");
        h.push_back("quality:        " + (quality_ ? quality_->activeName() + " (chosen: " + quality_->selectedName() + "; " + quality_->reason() + ")" : std::string("n/a (core/quality off)")));
        h.push_back("GL renderer:    " + (glRenderer_.empty() ? std::string("?") : glRenderer_));
        h.push_back("GL version:     " + (glVersion_.empty() ? std::string("?") : glVersion_));
        if (window_) {
            std::snprintf(b, sizeof b, "window:         %dx%d, %s", window_->width(), window_->height(), window_->fullscreen() ? "fullscreen" : "windowed");
            h.push_back(b);
            std::snprintf(b, sizeof b, "vsync:          swap interval %d (0 = off)", SDL_GL_GetSwapInterval());
            h.push_back(b);
        }
        if (render_) {
            std::snprintf(b, sizeof b, "render scale:   %.2f (%s)", render_->renderScale(), render_->worldBufferActive() ? "world drawn offscreen and stretched" : "world drawn straight to the window");
            h.push_back(b);
        }
        h.push_back(std::string("profiler mode: ") + (prof_->gpuMode() ? "GPU MODE (glFinish around render passes: slower, ranks passes)" : "CPU timing (normal)") +
                    ", hitch threshold " + engine::live::fmt1(prof_->hitchMs()) + " ms");
        h.push_back(std::string("game paused:   ") + (eng_->paused() ? "yes" : "no"));
        std::string text = prof_->liveDump(h);
        std::error_code ec;
        std::filesystem::create_directories("logs", ec);
        std::ofstream f("logs/profile_live.txt");
        if (f) f << text;
        toast_ = f ? "profile written to logs/profile_live.txt" : "COULD NOT WRITE logs/profile_live.txt";
        toastUntil_ = eng_->time() + 4.0;
        LOG_I("profiler", "%s", toast_.c_str());
    }

    engine::Engine* eng_ = nullptr;
    engine::Profiler* prof_ = nullptr;
    IInput* input_ = nullptr;
    UIHandler* ui_ = nullptr;
    IQuality* quality_ = nullptr;
    Window* window_ = nullptr;
    RenderEngine* render_ = nullptr;
    bool open_ = false;
    long dumpAt_ = -1, gpuToggleAt_ = -1;
    double snapshotDue_ = 0, toastUntil_ = 0;
    std::string glRenderer_, glVersion_, toast_;
};

REGISTER_MODULE(ProfilerOverlay);

} // namespace core
