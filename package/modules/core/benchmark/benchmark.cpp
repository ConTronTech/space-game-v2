// core/benchmark - `--benchmark[=seconds]` runs the current scene, measures REAL frame times (not the engine's clamped dt),
// prints a result block (console + logs/benchmark.txt) and quits. Add `--no-vsync` to measure what the machine can really do
// (with vsync on, frames are capped at the display rate and headroom is hidden). Inert without the flag. See docs/BENCHMARK.md.
#include <GL/gl.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
#include <SDL2/SDL.h>
#include "core/benchmark/bench_stats.h"
#include "core/quality/quality_api.h"
#include "core/window/window.h"
#include "engine/engine.h"
#include "engine/log.h"

namespace core {

class Benchmark : public engine::Module {
public:
    const char* name() const override { return "core/benchmark"; }
    std::vector<std::string> dependencies() const override { return {"core/window"}; }
    int priority() const override { return 1000; }   // last: measures the whole frame

    bool init(engine::Engine& eng) override {
        std::string v = eng.flagValue("benchmark");
        if (!eng.hasFlag("benchmark") && v.empty()) return true;   // inert
        active_ = true;
        if (!v.empty()) seconds_ = std::max(2.0, std::atof(v.c_str()));
        window_ = &eng.services.require<Window>();
        LOG_I("benchmark", "running for %.0f s after a %.0f s warm-up%s", seconds_, warmup_, eng.hasFlag("no-vsync") ? " (vsync off)" : " (vsync on: frames are capped at the display rate; add --no-vsync)");
        return true;
    }

    void onFrameEnd(engine::Engine& eng) override {
        if (!active_ || done_) return;
        auto now = std::chrono::steady_clock::now();
        if (!started_) { started_ = true; t0_ = last_ = now; return; }
        double ms = std::chrono::duration<double, std::milli>(now - last_).count();
        last_ = now;
        double elapsed = std::chrono::duration<double>(now - t0_).count();
        if (elapsed >= warmup_) frameMs_.push_back(ms);
        if (elapsed >= warmup_ + seconds_) finish(eng);
    }

private:
    void finish(engine::Engine& eng) {
        done_ = true;
        bench::Stats s = bench::compute(frameMs_);
        const char* renderer = (const char*)glGetString(GL_RENDERER);
        const char* version = (const char*)glGetString(GL_VERSION);
        auto* q = eng.services.get<IQuality>();
        std::string preset = q ? q->activeName() + (q->selectedName() == "auto" ? " (auto)" : "") : "n/a (core/quality off)";
        char b[1280];
        std::snprintf(b, sizeof b,
            "BENCHMARK RESULT\n"
            "  frames:          %d over %.1f s\n"
            "  average:         %.1f fps  (%.2f ms/frame)\n"
            "  1%% low:          %.1f fps  (99th-percentile frame %.2f ms)\n"
            "  worst frame:     %.2f ms\n"
            "  quality preset:  %s\n"
            "  vsync:           %s\n"
            "  resolution:      %dx%d\n"
            "  renderer:        %s | OpenGL %s\n"
            "  cpu / ram:       %d threads, %d MB\n"
            "  target:          60 fps average, 30 fps floor (docs/VISION.md)\n",
            s.frames, s.seconds, s.avgFps, s.avgMs, s.lowFps, s.p99Ms, s.worstMs, preset.c_str(),
            eng.hasFlag("no-vsync") ? "off" : "on", window_->width(), window_->height(),
            renderer ? renderer : "?", version ? version : "?", SDL_GetCPUCount(), SDL_GetSystemRAM());
        std::fputs(b, stdout);
        std::error_code ec;
        std::filesystem::create_directories("logs", ec);
        std::ofstream("logs/benchmark.txt") << b;
        LOG_I("benchmark", "done (also written to logs/benchmark.txt)");
        eng.quit();
    }

    bool active_ = false, started_ = false, done_ = false;
    double seconds_ = 20.0, warmup_ = 2.0;
    Window* window_ = nullptr;
    std::chrono::steady_clock::time_point t0_, last_;
    std::vector<double> frameMs_;
};

REGISTER_MODULE(Benchmark);

} // namespace core
