#pragma once
// cockpit::IShipModel: what other modules may know about the ship model. ship/cockpit provides it (read-only); optional per use, null-safe.
//   ship/ship_core asks drawnInChase() so it can skip its placeholder wireframe fighter. No service = nobody draws the real ship: the wireframe stays.
//   fx/particles asks thrusters() for the exhaust origins ('@THRUST-JET' faces of the model, docs/COCKPIT.md). Empty = no tag: use its fallback.
#include <vector>
#include "ship/cockpit/model_thrusters.h"

namespace cockpit {

class IShipModel {
public:
    virtual ~IShipModel() = default;
    virtual bool drawnInChase() const = 0;   // true: the ship model is loaded and drawn from outside in chase view
    virtual const std::vector<ThrusterJet>& thrusters() const = 0;   // ship frame (eye at origin, -z forward); valid after load, else empty
};

} // namespace cockpit
