#pragma once
// An InputMethod is one kind of device (keyboard, mouse, joystick, ...).
// The handler routes each JSON binding to the method whose device() matches its "device" field.
// To support a new device: write a module that implements this and calls
//     eng.services.require<core::IInput>().registerMethod(this);
// No other file needs to change.
#include <string>
#include "engine/json.h"

namespace core {

class IInput;

class InputMethod {
public:
    virtual ~InputMethod() = default;
    virtual const char* device() const = 0;   // the "device" value in JSON, e.g. "keyboard"
    // Called for each binding object in a profile. Return false and set err if it is invalid.
    virtual bool addBinding(const std::string& action, const engine::Json& binding, std::string& err) = 0;
    virtual void clearBindings() = 0;                    // profile is being replaced
    virtual void configure(const engine::Json&) {}       // profile "devices": { "<device>": {...} }
    // Once per frame: read the device and call in.contribute(action, value) for each active binding.
    virtual void poll(IInput& in) = 0;
    // Display name of this device's first binding for `action` ("G", "Left Click", ...), or "" if it has none / cannot name it.
    // Used by IInput::primaryBindingLabel for on-screen prompts. Optional: the default says "nothing readable".
    virtual std::string bindingLabel(const std::string&) const { return {}; }
};

// Display names shared by input methods (pure: no SDL needed, so tests can check them directly).
namespace input_label {
// SDL mouse button numbers: 1 left, 2 middle, 3 right, 4 x1, 5 x2
inline std::string mouseButton(int sdlButton) {
    switch (sdlButton) {
        case 1: return "Left Click";
        case 2: return "Middle Click";
        case 3: return "Right Click";
        case 4: return "Mouse 4";
        case 5: return "Mouse 5";
        default: return {};
    }
}
// mouse method axis codes: 0 x, 1 y, 2 wheel
inline std::string mouseAxis(int code) { return code == 0 ? "Mouse X" : code == 1 ? "Mouse Y" : code == 2 ? "Mouse Wheel" : std::string(); }
} // namespace input_label

} // namespace core
