#pragma once
// Whoever the camera should follow (the ship) publishes a pose; core/camera turns it into the view.
//     class Ship : public core::ITransformSource { core::Pose transform(float alpha) const override {...} };
//     eng.services.provide<core::ITransformSource>(this);
// 'alpha' is Engine::alpha(): blend previous->current physics state so motion is smooth at any frame rate.
#include "engine/math.h"

namespace core {

struct Pose {
    engine::Vec3 pos, fwd{0, 0, -1}, up{0, 1, 0};
};

class ITransformSource {
public:
    virtual ~ITransformSource() = default;
    virtual Pose transform(float alpha) const = 0;
};

enum class CameraMode { Cockpit = 0, Chase = 1 };

class ICamera {
public:
    virtual ~ICamera() = default;
    virtual CameraMode mode() const = 0;
    virtual void setMode(CameraMode m) = 0;
    virtual void cycleMode() = 0;
    virtual bool showsShip() const = 0;   // true when the camera is outside the ship: the ship should draw itself
};

} // namespace core
