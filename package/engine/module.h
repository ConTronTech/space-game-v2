#pragma once
// The one interface every piece of the game implements.
// Override only the hooks you need; everything has a no-op default.
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace engine {

class Engine;

class Module {
public:
    virtual ~Module() = default;

    // ---- identity ----
    virtual const char* name() const = 0;                          // unique, e.g. "core/input_handler"
    virtual std::vector<std::string> dependencies() const { return {}; } // names of modules that must init first
    // Init after these modules IF they are loaded, but do not fail when they are absent (optional services).
    virtual std::vector<std::string> optionalDependencies() const { return {}; }
    virtual int priority() const { return 0; }                     // lower = earlier (init order & per-phase order)
    virtual bool required() const { return false; }                // true: engine aborts if init fails

    // ---- lifecycle ----
    virtual bool init(Engine&) { return true; }  // return false to fail (module gets disabled)
    virtual void shutdown(Engine&) {}

    // ---- frame phases, in the order they run ----
    virtual void onFrameBegin(Engine&) {}              // poll OS events, clear buffers
    virtual void onFixedUpdate(Engine&, float dt) {}   // fixed timestep (physics), may run 0..N times per frame
    virtual void onUpdate(Engine&, float dt) {}        // once per frame, variable dt (game logic)
    virtual void onRender(Engine&) {}                  // 3D world (usually via RenderEngine passes instead)
    virtual void onRenderUI(Engine&) {}                // 2D overlay (usually via UIHandler panels instead)
    virtual void onFrameEnd(Engine&) {}                // clear per-frame state
    virtual void onPresent(Engine&) {}                 // swap buffers

    // Boot-time only: called by loadModules()'s startup replay, AFTER onRenderUI, so anything drawn here sits on top of
    // every other module's onRenderUI content that frame - a guaranteed-visible overlay for the one thing that must never
    // be hidden while the engine is still starting up (the loading indicator, core/boot_screen). Never called once real
    // gameplay frames begin; a module with nothing boot-time-critical to say never needs to override it.
    virtual void onBootOverlay(Engine&) {}
};

// ---- self-registration ----
using ModuleFactory = std::function<std::unique_ptr<Module>()>;

struct ModuleRegistry {
    static std::vector<ModuleFactory>& factories() {
        static std::vector<ModuleFactory> f; // function-local: safe static-init order
        return f;
    }
};

struct ModuleRegistrar {
    explicit ModuleRegistrar(ModuleFactory f) { ModuleRegistry::factories().push_back(std::move(f)); }
};

} // namespace engine

// Put once at the bottom of your module's .cpp. That's all the wiring there is.
#define REGISTER_MODULE(Type) \
    static ::engine::ModuleRegistrar registrar_##Type([] { return std::make_unique<Type>(); })
