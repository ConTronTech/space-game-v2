// core/input_methods/keyboard - "device": "keyboard"
//   { "device": "keyboard", "key": "W", "scale": 1 }       // key names are SDL scancode names: "W", "Space", "Left Shift", "Up", "Escape"
#include <SDL2/SDL.h>
#include "core/input_handler/input_api.h"
#include "core/input_handler/input_method.h"
#include "engine/engine.h"

class KeyboardMethod : public engine::Module, public core::InputMethod {
public:
    const char* name() const override { return "core/input_methods/keyboard"; }
    std::vector<std::string> dependencies() const override { return {"core/input_handler"}; }
    bool init(engine::Engine& eng) override {
        in_ = &eng.services.require<core::IInput>();
        in_->registerMethod(this);
        return true;
    }
    void shutdown(engine::Engine&) override { in_->unregisterMethod(this); }

    const char* device() const override { return "keyboard"; }
    void clearBindings() override { bindings_.clear(); }
    bool addBinding(const std::string& action, const engine::Json& b, std::string& err) override {
        std::string key = b["key"].str();
        SDL_Scancode sc = SDL_GetScancodeFromName(key.c_str());
        if (key.empty() || sc == SDL_SCANCODE_UNKNOWN) { err = "unknown key '" + key + "'"; return false; }
        bindings_.push_back({action, sc, (float)b["scale"].num(1.0)});
        return true;
    }
    void poll(core::IInput& in) override {
        const Uint8* state = SDL_GetKeyboardState(nullptr);
        for (auto& b : bindings_) if (state[b.key]) in.contribute(b.action, b.scale);
    }
    std::string bindingLabel(const std::string& action) const override {
        for (auto& b : bindings_)
            if (b.action == action) { const char* n = SDL_GetScancodeName(b.key); if (n && *n) return n; }
        return {};
    }

private:
    struct Binding { std::string action; SDL_Scancode key; float scale; };
    std::vector<Binding> bindings_;
    core::IInput* in_ = nullptr;
};

REGISTER_MODULE(KeyboardMethod);
