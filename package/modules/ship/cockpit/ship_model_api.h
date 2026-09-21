#pragma once
// cockpit::IShipModel: tells other modules whether the real ship model is drawn in chase view. ship/cockpit provides it (read-only);
// ship/ship_core asks it so it can skip its placeholder wireframe fighter. No service = nobody draws the real ship: the wireframe stays.
namespace cockpit {

class IShipModel {
public:
    virtual ~IShipModel() = default;
    virtual bool drawnInChase() const = 0;   // true: the ShipV2 model is loaded and drawn from outside in chase view
};

} // namespace cockpit
