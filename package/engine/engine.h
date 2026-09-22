#pragma once
#include <string>
#include <unordered_set>
#include <vector>

#include "engine/config.h"
#include "engine/event_bus.h"
#include "engine/module.h"
#include "engine/services.h"

namespace engine {

// Emitted by anything that wants the game to close (window X button, menu, ...).
struct QuitRequested {};
// Emitted when the game is paused/unpaused (see Engine::setPaused).
struct PauseChanged { bool paused; };
// Emitted once per module as loadModules() finishes it (index/total are 1-based; index == total on the last one).
// A boot-progress module (e.g. core/boot_screen) can subscribe to this to draw a loading bar - see docs/STARTUP.md.
struct ModuleLoaded { std::string name; int index = 0, total = 0; };

class Engine {
public:
    EventBus events;
    Services services;
    Config config;   // tunables: config.get("module.key", default, "what it does")

    // Runs the game until quit. Returns process exit code.
    int run(int argc, char** argv);
    void quit() { running_ = false; }

    // Pausing freezes onFixedUpdate (physics/simulation). Everything else keeps running so
    // menus, input and rendering still work. Modules react to the PauseChanged event.
    bool paused() const { return paused_; }
    void setPaused(bool p) { if (p != paused_) { paused_ = p; events.emit(PauseChanged{p}); } }

    // Command line: --flag, --key=value
    bool hasFlag(const std::string& flag) const;
    std::string flagValue(const std::string& key, const std::string& fallback = "") const;

    double time() const { return time_; }       // seconds since start
    // 0..1: how far this frame is between the last and the next fixed step. Render code blends its
    // previous and current physics state by this so motion is smooth at any display rate.
    float alpha() const { return alpha_; }
    unsigned long frame() const { return frame_; }
    Module* findModule(const std::string& name) const;

private:
    bool loadModules();

    std::vector<std::unique_ptr<Module>> modules_; // final, dependency-sorted, initialised
    std::vector<std::string> args_;
    bool running_ = false;
    bool paused_ = false;
    double time_ = 0;
    float alpha_ = 1.0f;
    unsigned long frame_ = 0;
};

} // namespace engine
