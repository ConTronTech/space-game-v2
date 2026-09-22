// core/camera - owns the view. Reads the ship's pose (core::ITransformSource) and sets RenderEngine's camera.
// Modes: Cockpit (eye at the ship) and Chase (a rigid offset behind and above, same orientation as the ship; the ship model is drawn from outside). Action "camera_next" (V) cycles.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "core/camera/camera_api.h"
#include "core/camera/camera_math.h"
#include "core/input_handler/input_api.h"
#include "core/render_engine/render_engine.h"
#include "core/settings/settings_api.h"
#include "engine/engine.h"
#include "engine/log.h"

namespace core {

class CameraController : public engine::Module, public ICamera {
public:
    const char* name() const override { return "core/camera"; }
    std::vector<std::string> dependencies() const override { return {"core/render_engine", "core/input_handler"}; }

    bool init(engine::Engine& eng) override {
        render_ = &eng.services.require<RenderEngine>();
        input_ = &eng.services.require<IInput>();
        settings_ = eng.services.get<ISettings>();
        chaseDistance_ = eng.config.get("camera.chase_distance", 27.0f, "chase camera: metres behind the pilot's eye, along the ship's forward (the ShipV3 model is 13.6 m long and reaches 10.1 m behind the eye)");
        chaseHeight_ = eng.config.get("camera.chase_height", 7.0f, "chase camera: metres above the pilot's eye, along the ship's up");
        jitterLog_ = std::atol(eng.flagValue("camera-jitter-log", "0").c_str());   // dev diagnostic, see measureJitter (docs/PRECISION.md)
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
        measureJitter(view, render_->camera.view);
    }

    // --camera-jitter-log=N (dev diagnostic only, docs/PRECISION.md). The render chain past the ship's new double position is still FLOAT
    // end to end: viewMatrix() stores the eye as -dot(axis, pose.pos) in floats, and every camera-relative world pass (world/star_system,
    // world/asteroids, world/stations) recovers the eye back out of that matrix. So the eye the world is drawn around is not the ship's true
    // position but a rounded one, and the rounding CHANGES every frame - that difference is the wobble a pilot sees in the whole world.
    // This logs it: the error against the true double eye, and the frame-to-frame change of that error, which IS the jitter, in metres.
    void measureJitter(const Pose& view, const float m[16]) {
        if (jitterLog_ <= 0) return;
        jitterLog_--;
        const engine::Vec3d eye{                                                 // exactly what star_system.cpp / asteroids.cpp / stations.cpp do
            -((double)m[0] * m[12] + (double)m[1] * m[13] + (double)m[2] * m[14]),
            -((double)m[4] * m[12] + (double)m[5] * m[13] + (double)m[6] * m[14]),
            -((double)m[8] * m[12] + (double)m[9] * m[13] + (double)m[10] * m[14])};
        const engine::Vec3d err{eye.x - view.posD.x, eye.y - view.posD.y, eye.z - view.posD.z};
        const double e = std::sqrt(err.x * err.x + err.y * err.y + err.z * err.z);
        double jitter = 0;
        if (jitterHave_) {
            const engine::Vec3d d{err.x - prevErr_.x, err.y - prevErr_.y, err.z - prevErr_.z};
            jitter = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            jitterWorst_ = std::max(jitterWorst_, jitter);
            jitterSum_ += jitter; jitterN_++;
        }
        prevErr_ = err; jitterHave_ = true;
        worstErr_ = std::max(worstErr_, e);
        const double dist = std::sqrt(view.posD.x * view.posD.x + view.posD.y * view.posD.y + view.posD.z * view.posD.z);
        LOG_I("camera", "jitter: |eye| %.1f, recovered eye is off by %.4f m (worst %.4f), moved %.4f m since last frame (worst %.4f, mean %.4f over %ld)",
              dist, e, worstErr_, jitter, jitterWorst_, jitterN_ > 0 ? jitterSum_ / (double)jitterN_ : 0.0, jitterN_);
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
    long jitterLog_ = 0, jitterN_ = 0;                        // --camera-jitter-log
    engine::Vec3d prevErr_;
    bool jitterHave_ = false;
    double worstErr_ = 0, jitterWorst_ = 0, jitterSum_ = 0;
};

REGISTER_MODULE(CameraController);

} // namespace core
