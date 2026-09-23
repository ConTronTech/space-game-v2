#pragma once
// core/input_handler - device-independent actions driven by JSON profiles.
//
// Game modules only ask about actions:
//     auto& in = eng.services.require<core::IInput>();
//     float thrust = in.value("thrust");     // -1..1, from keys, mouse, stick... whatever the profile says
//     if (in.pressed("fire")) ...             // went past 0.5 this frame - call from onUpdate, NOT onFixedUpdate
// Which physical inputs drive an action lives in config/input/<profile>.json, not in code.
// Devices are pluggable InputMethod modules (see input_method.h). See docs/INPUT.md.
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>
#include "core/input_handler/input_api.h"
#include "core/input_handler/input_method.h"
#include "engine/module.h"

namespace core {

class InputHandler : public engine::Module, public IInput {
public:
    const char* name() const override { return "core/input_handler"; }
    std::vector<std::string> dependencies() const override { return {"core/window"}; }
    bool required() const override { return true; }
    bool init(engine::Engine&) override;
    void shutdown(engine::Engine&) override;
    void onFrameBegin(engine::Engine&) override;

    // ---- for game modules ----
    float value(const std::string& action) const override;
    bool down(const std::string& action) const override { return std::abs(value(action)) > kThreshold; }
    bool pressed(const std::string& action) const override;
    bool released(const std::string& action) const override;

    // ---- for input methods ----
    void registerMethod(InputMethod* m) override { methods_[m->device()] = m; needLoad_ = true; }
    void unregisterMethod(InputMethod* m) override { methods_.erase(m->device()); }
    void contribute(const std::string& action, float v) override { cur_[action] += v; }
    void consume(const std::string& action) override { cur_[action] = 0.0f; }
    std::string primaryBindingLabel(const std::string& action) const override;

    // Load config/input/<name>.json now (replaces the current profile). Returns false on error.
    bool loadProfile(const std::string& name) override;
    const std::string& profile() const override { return profile_; }

private:
    static constexpr float kThreshold = 0.5f;
    static float clamp1(float v) { return v < -1 ? -1 : (v > 1 ? 1 : v); }
    float prevValue(const std::string& a) const;

    std::unordered_map<std::string, InputMethod*> methods_;
    std::unordered_map<std::string, float> cur_, prev_;
    std::string profile_ = "default";
    bool needLoad_ = true;
    bool firstPoll_ = true;
};

} // namespace core
