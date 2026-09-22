#pragma once
// core/debugger - the in-game GAMEPLAY/STATE debugger (F6 panel, F7 draws on/off, F8 dump to logs/debug_state.txt). See docs/DEBUGGER.md.
// An OPTIONAL service: any module may plug in without the debugger knowing anything about it.
//
//     if (auto* dbg = eng.services.get<core::IDebug>()) {
//         dbg->watch("gravity.dominant", [this] { return dominantName_; });          // grouped under "gravity" (text before the first '.')
//         dbg->drawHook("gravity.accel", [this](core::RenderEngine& r) { drawAccel(r); });
//         dbg->logEvent("gravity: dominant body changed to " + dominantName_);
//     }
//     // in shutdown(): dbg->unwatch("gravity.dominant"); dbg->removeDrawHook("gravity.accel");
//
// Watch getters run only while the F6 panel is open (a few times a second, debugger.watch_hz), never when it is closed.
// Draw hooks run only when switched on in the panel AND F7 has draws visible, inside the debugger's own render pass:
//   a hook may assume: GL_MODELVIEW is current and loaded with the camera view (translation included, so world coordinates work
//   for things near the origin; for far-away things subtract the camera position in double first, see docs/DEBUGGER.md),
//   lighting/texturing/culling are OFF, blending ON (SRC_ALPHA, ONE_MINUS_SRC_ALPHA), depth test ON, depth writes OFF, line width 1.5.
//   a hook must: leave the matrix stacks as it found them (push/pop what it changes), and push/pop any other GL state it changes.
// Registering the same name again replaces the old getter / hook (the last registration wins, a warning is logged).
#include <functional>
#include <string>

namespace core {

class RenderEngine;

class IDebug {
public:
    virtual ~IDebug() = default;
    virtual void watch(const std::string& name, std::function<std::string()> getter) = 0;
    virtual void unwatch(const std::string& name) = 0;
    virtual void drawHook(const std::string& name, std::function<void(RenderEngine&)> fn) = 0;
    virtual void removeDrawHook(const std::string& name) = 0;
    virtual void logEvent(const std::string& line) = 0;      // one line in the event log (the debugger adds the game time)
};

} // namespace core
