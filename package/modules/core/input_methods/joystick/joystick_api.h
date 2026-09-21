#pragma once
// What the joystick input method exposes: for a controls settings screen, the profiler / debug overlays and tests.
//
//     auto* pads = eng.services.get<core::IControllers>();          // null if the module is off
//     for (int i = 0; i < pads->deviceCount(); i++) { pads->devices()[i].name; pads->rawAxis(i, 0); pads->rawButton(i, 3); }
//
// One physical device exposes MANY controls: a steering wheel base is one device with steering, pedal axes, shifter and wheel buttons and a hat.
#include <string>
#include <vector>

namespace core {

struct ControllerInfo {
    int index = 0;                   // slot number (stable while the device stays plugged in)
    std::string name;
    int vid = 0, pid = 0;
    std::string guid;
    int axes = 0, buttons = 0, hats = 0;
    std::string profile;             // the matched profile's name, or "(generic fallback: maps nothing)"
    bool fake = false;               // created by --fake-joystick
};

class IControllers {
public:
    virtual ~IControllers() = default;
    virtual const std::vector<ControllerInfo>& devices() const = 0;
    virtual int deviceCount() const = 0;
    virtual int rawAxis(int device, int axis) const = 0;         // int16 as the device reports it (0 for a bad index)
    virtual bool rawButton(int device, int button) const = 0;
    virtual int rawHat(int device, int hat) const = 0;           // bitmask 1 up, 2 right, 4 down, 8 left
    virtual const std::string& lastEvent() const = 0;            // "PXN-V10 axis 2 = 512" (empty until something moved)
    virtual void reloadProfiles() {}                             // re-read config/input/devices/*.json and re-match every open device (no restart)
};

} // namespace core
