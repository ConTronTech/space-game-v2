#pragma once
// An InputMethod is one kind of device (keyboard, mouse, joystick, ...).
// The handler routes each JSON binding to the method whose device() matches its "device" field.
// To support a new device: write a module that implements this and calls
//     eng.services.require<core::InputHandler>().registerMethod(this);
// No other file needs to change.
#include <string>
#include "engine/json.h"

namespace core {

class InputHandler;

class InputMethod {
public:
    virtual ~InputMethod() = default;
    virtual const char* device() const = 0;   // the "device" value in JSON, e.g. "keyboard"
    // Called for each binding object in a profile. Return false and set err if it is invalid.
    virtual bool addBinding(const std::string& action, const engine::Json& binding, std::string& err) = 0;
    virtual void clearBindings() = 0;                    // profile is being replaced
    virtual void configure(const engine::Json&) {}       // profile "devices": { "<device>": {...} }
    // Once per frame: read the device and call in.contribute(action, value) for each active binding.
    virtual void poll(InputHandler& in) = 0;
};

} // namespace core
