// core/camera - owns the view. Reads the ship's pose (core::ITransformSource) and sets RenderEngine's camera.
// Modes: Cockpit (eye at the ship) and Chase (a rigid offset behind and above, same orientation as the ship; the ship model is drawn from outside). Action "camera_next" (V) cycles.
#include <algorithm>
#include "core/camera/camera_api.h"
#include "core/camera/camera_math.h"
#include "core/input_handler/input_api.h"
#include "core/render_engine/render_engine.h"
#include "core/settings/settings_api.h"
#include "engine/engine.h"

namespace core {

class CameraController : public engine::Module, public ICamera {
public:
    const char* name() const override { return "core/camera"; }
    std::vector<std::string> dependencies() const override { return {"core/render_engine", "core/input_handler"}; }

    bool init(engine::Engine& eng) override {
        render_ = &eng.services.require<RenderEngine>();
        input_ = &eng.services.require<IInput>();
        settings_ = eng.services.get<ISettings>();
        chaseDistance_ = eng.config.get("camera.chase_distance", 24.0f, "chase camera: metres behind the pilot's eye, along the ship's forward (the ShipV2 model is 11.8 m long and reaches 8.7 m behind the eye)");
        chaseHeight_ = eng.config.get("camera.chase_height", 6.0f, "chase camera: metres above the pilot's eye, along the ship's up");
        if (settings_) mode_ = (CameraMode)std::clamp(settings_->get("camera.mode", 0), 0, 1);
        eng.services.provide<ICamera>(this);
        return true;
    }
    void shutdown(engine::Engine& eng) override { eng.services.withdraw<ICamera>(); }

    void onUpdate(engine::Engine& eng, float) override {
        if (input_->pressed("camera_next")) cycleMode();
        auto* src = eng.services.get<ITransformSource>();   // no ship yet? keep whatever view is set
        if (!src) return;
        Pose ship = src->transform(eng.alpha());
        Pose view = mode_ == CameraMode::Chase ? cam::chasePose(ship, chaseDistance_, chaseHeight_) : ship;
        cam::viewMatrix(view, render_->camera.view);
    }

    CameraMode mode() const override { return mode_; }
    void setMode(CameraMode m) override { mode_ = m; if (settings_) settings_->set("camera.mode", (int)m); }
    void cycleMode() override { setMode(mode_ == CameraMode::Cockpit ? CameraMode::Chase : CameraMode::Cockpit); }
    bool showsShip() const override { return mode_ == CameraMode::Chase; }

private:
    RenderEngine* render_ = nullptr;
    IInput* input_ = nullptr;
    ISettings* settings_ = nullptr;
    CameraMode mode_ = CameraMode::Cockpit;
    float chaseDistance_ = 24.0f, chaseHeight_ = 6.0f;
};

REGISTER_MODULE(CameraController);

} // namespace core
