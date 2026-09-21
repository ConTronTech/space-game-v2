#pragma once
// core::IDisplays - what the window knows about the screens, for anyone who must adapt to them (quality, UI, tools).
//     auto* d = eng.services.get<core::IDisplays>();          // null if core/window is off
//     d->list() -> every display (name, bounds, desktop mode, refresh, DPI, modes);  d->chosen() -> the one the game was asked to open on
//     d->current() -> the display the window is on NOW (it follows the window when you drag it);  d->drawableSize(), d->refreshHz(), d->aspect()
// Events (engine.events): core::DisplayChanged when the window moved to another display or the display list changed. See docs/DISPLAYS.md.
#include <vector>
#include "core/window/display_rules.h"

namespace core {

struct DisplayChanged { int index = 0; int w = 0, h = 0; };   // the display the window is on now, and its desktop size

class IDisplays {
public:
    virtual ~IDisplays() = default;
    virtual const std::vector<display::DisplayInfo>& list() const = 0;
    virtual int chosen() const = 0;                     // index the startup logic picked (flag > setting > auto)
    virtual int current() const = 0;                    // index of the display the window is on now (falls back to chosen())
    virtual void drawableSize(int& w, int& h) const = 0;   // pixels the GL context renders to
    virtual int refreshHz() const = 0;                  // refresh of the current display's desktop mode, 0 if unknown
    virtual display::Aspect aspect() const = 0;         // aspect class of the drawable area (what the player sees)
};

} // namespace core
