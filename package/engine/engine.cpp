#include "engine/engine.h"
#include "engine/log.h"
#include "engine/profiler.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>

namespace engine {

bool Engine::hasFlag(const std::string& flag) const {
    return std::find(args_.begin(), args_.end(), "--" + flag) != args_.end();
}

std::string Engine::flagValue(const std::string& key, const std::string& fallback) const {
    std::string prefix = "--" + key + "=";
    for (auto& a : args_)
        if (a.rfind(prefix, 0) == 0) return a.substr(prefix.size());
    return fallback;
}

Module* Engine::findModule(const std::string& name) const {
    for (auto& m : modules_) if (name == m->name()) return m.get();
    return nullptr;
}

static std::unordered_set<std::string> splitList(const std::string& s) {
    std::unordered_set<std::string> out;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        if (j > i) out.insert(s.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

// Instantiate every registered module, drop disabled/duplicate ones, sort by
// dependency (ties broken by priority then name), then init in that order.
bool Engine::loadModules() {
    // --disable=a,b  and/or  --disable=a --disable=b
    std::unordered_set<std::string> disabled;
    for (auto& a : args_)
        if (a.rfind("--disable=", 0) == 0) for (auto& n : splitList(a.substr(10))) disabled.insert(n);
    std::map<std::string, std::unique_ptr<Module>> pending;

    for (auto& make : ModuleRegistry::factories()) {
        auto m = make();
        std::string n = m->name();
        if (disabled.count(n)) { LOG_I("engine", "disabled by flag: %s", n.c_str()); continue; }
        if (pending.count(n)) { LOG_W("engine", "duplicate module name '%s' - ignoring second", n.c_str()); continue; }
        pending[n] = std::move(m);
    }

    std::vector<std::unique_ptr<Module>> sorted;
    std::unordered_set<std::string> placed;
    while (!pending.empty()) {
        // Pick the ready module with the lowest priority. Optional dependencies are honoured while possible;
        // if that leaves nothing ready (an optional cycle) they are ignored, so they can never drop a module.
        Module* best = nullptr;
        for (bool useOptional : {true, false}) {
            for (auto& [n, m] : pending) {
                bool ready = true;
                for (auto& d : m->dependencies()) if (!placed.count(d)) { ready = false; break; }
                if (ready && useOptional)
                    for (auto& d : m->optionalDependencies()) if (d != n && pending.count(d)) { ready = false; break; }
                if (ready && (!best || m->priority() < best->priority())) best = m.get();
            }
            if (best) break;
        }
        if (!best) break;
        std::string n = best->name();
        placed.insert(n);
        sorted.push_back(std::move(pending[n]));
        pending.erase(n);
    }
    for (auto& [n, m] : pending) {
        LOG_W("engine", "module '%s' skipped: missing or cyclic dependency", n.c_str());
        if (m->required()) return false;
    }

    std::unordered_set<std::string> alive;
    const int total = (int)sorted.size();
    int index = 0;
    for (auto& m : sorted) {
        std::string n = m->name();
        index++;
        bool depsOk = true;
        for (auto& d : m->dependencies()) if (!alive.count(d)) { depsOk = false; break; }
        if (!depsOk) {
            LOG_W("engine", "module '%s' skipped: dependency failed", n.c_str());
            if (m->required()) return false;
            continue;
        }
        bool ok = false;
        try { ok = m->init(*this); }
        catch (const std::exception& e) { LOG_E("engine", "module '%s' threw during init: %s", n.c_str(), e.what()); }
        catch (...) { LOG_E("engine", "module '%s' threw during init", n.c_str()); }
        if (!ok) {
            LOG_E("engine", "module '%s' failed to init", n.c_str());
            if (m->required()) return false;
            continue;
        }
        LOG_I("engine", "loaded %s", n.c_str());
        alive.insert(n);
        modules_.push_back(std::move(m));

        // Boot progress (docs/STARTUP.md): a slow module (asset decoding, mesh generation, ...) must never leave the window
        // unresponsive with no event pump, or the OS/compositor can visually read it as a frozen app even with no real hang.
        // Replay the SAME per-frame hooks the main loop uses, over whatever modules have loaded so far: before core/window
        // exists this is a no-op; once it does, onFrameBegin pumps events, and any boot-progress module (e.g. core/boot_screen,
        // which draws the actual bar) reacts to the ModuleLoaded event below and paints in onRenderUI; onPresent swaps.
        events.emit(ModuleLoaded{n, index, total});
        for (auto& loaded : modules_) loaded->onFrameBegin(*this);
        for (auto& loaded : modules_) loaded->onRenderUI(*this);
        for (auto& loaded : modules_) loaded->onPresent(*this);
    }
    return true;
}

int Engine::run(int argc, char** argv) {
    for (int i = 1; i < argc; i++) args_.push_back(argv[i]);

    config.load();
    if (!log::setLevel(config.get<std::string>("engine.log_level", "info", "debug | info | warn | error")))
        LOG_W("engine", "engine.log_level must be debug, info, warn or error - using info");
    log::openFile(config.get<std::string>("engine.log_file", "logs/game.log", "log file, overwritten each run; empty = console only"));
    events.subscribe<QuitRequested>([this](const QuitRequested&) { quit(); });

    // Profiling (docs/PERFORMANCE.md). Provided as a service BEFORE the modules load so RenderEngine can time its passes.
    //   LIVE (profiler.lite, default true): a ring of the last 600 frames + hitch capture for the in-game overlay (F3) and the F5 dump: about 220 clock reads a frame.
    //   --profile: the DETAILED cumulative table + slow frames, printed at exit;  --profile=gpu adds glFinish around passes.
    Profiler profiler;
    const bool detailed = hasFlag("profile") || !flagValue("profile").empty();
    const bool lite = config.get("profiler.lite", true, "always-on lite profiler: keeps the last 600 frames for the in-game overlay (F3) and the F5 dump; the cost is a few microseconds per frame");
    profiler.setHitchMs(config.get("profiler.hitch_ms", 40.0f, "a frame slower than this many ms is remembered as a hitch (the overlay counts them, F5 lists them)"));
    Profiler* prof = (detailed || lite) ? &profiler : nullptr;
    if (prof) {
        services.provide<Profiler>(prof);
        profiler.setDetailed(detailed);
        profiler.enableLive(lite);
        profiler.setGpuMode(flagValue("profile") == "gpu");
        profiler.setSlowMs(std::atof(flagValue("profile-slow", "0").c_str()));
    }

    if (!loadModules()) {
        LOG_E("engine", "a required module failed - aborting");
        for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) (*it)->shutdown(*this);
        return 1;
    }
    if (hasFlag("list-modules")) {
        for (auto& m : modules_) std::printf("%s\n", m->name());
        for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) (*it)->shutdown(*this);
        return 0;
    }

    const float hz = std::clamp(config.get("engine.fixed_hz", 60.0f, "physics steps per second (10 - 480)"), 10.0f, 480.0f);
    const float step = 1.0f / hz;
    const long maxFrames = std::atol(flagValue("frames", "0").c_str()); // 0 = unlimited (smoke tests use >0)
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    float acc = 0;
    running_ = true;

    // profiling ids: one per module and hook, e.g. "core/window:present" (only built with --profile)
    static const char* const kHooks[7] = {"begin", "fixed", "update", "render", "ui", "end", "present"};
    std::vector<int> hookIds[7];
    int idOther = -1;
    if (prof) {
        for (int h = 0; h < 7; h++)
            for (auto& m : modules_) hookIds[h].push_back(prof->intern(std::string(m->name()) + ":" + kHooks[h]));
        idOther = prof->intern("engine:unaccounted");
    }
    double phaseMs = 0;   // time inside module hooks this frame (profiling only)
    auto runPhase = [&](int hook, auto&& fn) {
        if (!prof) { for (size_t i = 0; i < modules_.size(); i++) fn(*modules_[i]); return; }
        auto t0 = clock::now();                                    // one clock read per module: each end time is the next start time
        for (size_t i = 0; i < modules_.size(); i++) {
            fn(*modules_[i]);
            auto t1 = clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            prof->add(hookIds[hook][i], ms);
            phaseMs += ms;
            t0 = t1;
        }
    };
    auto closeProfiledFrame = [&](double frameMs) {
        prof->add(idOther, std::max(0.0, frameMs - phaseMs));
        prof->endFrame(frameMs, paused_);
    };

    while (running_) {
        auto now = clock::now();
        float rawDt = std::chrono::duration<float>(now - last).count();
        float dt = rawDt;
        last = now;
        dt = std::min(dt, 0.1f); // don't spiral after a stall
        if (prof) {
            if (frame_ > 0) closeProfiledFrame(rawDt * 1000.0);   // the frame that just ended, swap included
            prof->beginFrame(frame_, paused_, time_);
            phaseMs = 0;
        }
        time_ += dt;
        acc += dt;

        runPhase(0, [&](Module& m) { m.onFrameBegin(*this); });
        if (paused_) acc = 0; // don't fast-forward the simulation after resuming
        while (acc >= step) {
            runPhase(1, [&](Module& m) { m.onFixedUpdate(*this, step); });
            acc -= step;
        }
        alpha_ = paused_ ? 1.0f : std::clamp(acc / step, 0.0f, 1.0f); // paused: show the frozen state exactly
        runPhase(2, [&](Module& m) { m.onUpdate(*this, dt); });
        runPhase(3, [&](Module& m) { m.onRender(*this); });
        runPhase(4, [&](Module& m) { m.onRenderUI(*this); });
        runPhase(5, [&](Module& m) { m.onFrameEnd(*this); });
        runPhase(6, [&](Module& m) { m.onPresent(*this); });

        frame_++;
        if (maxFrames > 0 && (long)frame_ >= maxFrames) quit();
    }

    if (prof) closeProfiledFrame(std::chrono::duration<double, std::milli>(clock::now() - last).count());
    if (prof && detailed) {
        std::string report = prof->report();
        std::fputs(report.c_str(), stdout);
        std::error_code ec;
        std::filesystem::create_directories("logs", ec);
        std::ofstream("logs/profile.txt") << report;
        LOG_I("engine", "profile written to logs/profile.txt");
    }
    if (prof) services.withdraw<Profiler>();

    for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) (*it)->shutdown(*this);
    modules_.clear();
    config.save();
    return 0;
}

} // namespace engine
