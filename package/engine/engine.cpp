#include "engine/engine.h"
#include "engine/log.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
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
        Module* best = nullptr;
        for (auto& [n, m] : pending) {
            bool ready = true;
            for (auto& d : m->dependencies()) if (!placed.count(d)) { ready = false; break; }
            if (ready && (!best || m->priority() < best->priority())) best = m.get();
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
    for (auto& m : sorted) {
        std::string n = m->name();
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

    while (running_) {
        auto now = clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        dt = std::min(dt, 0.1f); // don't spiral after a stall
        time_ += dt;
        acc += dt;

        for (auto& m : modules_) m->onFrameBegin(*this);
        if (paused_) acc = 0; // don't fast-forward the simulation after resuming
        while (acc >= step) {
            for (auto& m : modules_) m->onFixedUpdate(*this, step);
            acc -= step;
        }
        alpha_ = paused_ ? 1.0f : std::clamp(acc / step, 0.0f, 1.0f); // paused: show the frozen state exactly
        for (auto& m : modules_) m->onUpdate(*this, dt);
        for (auto& m : modules_) m->onRender(*this);
        for (auto& m : modules_) m->onRenderUI(*this);
        for (auto& m : modules_) m->onFrameEnd(*this);
        for (auto& m : modules_) m->onPresent(*this);

        frame_++;
        if (maxFrames > 0 && (long)frame_ >= maxFrames) quit();
    }

    for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) (*it)->shutdown(*this);
    modules_.clear();
    config.save();
    return 0;
}

} // namespace engine
