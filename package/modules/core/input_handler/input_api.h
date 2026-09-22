#pragma once
// The input service other modules depend on. Ask for the interface, not the class:
//     auto& in = eng.services.require<core::IInput>();
// Any module that provides core::IInput can replace core/input_handler.
#include <string>

namespace core {

class InputMethod;

class IInput {
public:
    virtual ~IInput() = default;

    // ---- for game modules ----
    virtual float value(const std::string& action) const = 0;      // -1..1, summed over sources
    virtual bool down(const std::string& action) const = 0;        // |value| > 0.5
    virtual bool pressed(const std::string& action) const = 0;     // crossed this frame (use in onUpdate)
    virtual bool released(const std::string& action) const = 0;

    // ---- for input methods (devices) ----
    virtual void registerMethod(InputMethod* m) = 0;
    virtual void unregisterMethod(InputMethod* m) = 0;
    virtual void contribute(const std::string& action, float v) = 0;

    // Zeroes an action's accumulated value for the REST of the current frame (value()/down()/pressed()/released() all read
    // as if nothing contributed to it), so a UI layer that "swallows" a key press (e.g. Esc closes a menu instead of also
    // opening the pause menu) does not have to guess at a magic offset to subtract. Only affects this frame: next frame's
    // fresh poll is unaffected. Non-pure (default no-op) so any other IInput implementation keeps compiling.
    virtual void consume(const std::string&) {}

    // ---- profiles ----
    virtual bool loadProfile(const std::string& name) = 0;
    virtual const std::string& profile() const = 0;
};

} // namespace core
