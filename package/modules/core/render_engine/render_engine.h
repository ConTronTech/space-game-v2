#pragma once
// core/render_engine - camera + ordered render passes.
// Modules don't draw from onRender(); they register a pass:
//     auto& r = eng.services.require<core::RenderEngine>();
//     r.addPass("my_stuff", 100, [](core::RenderEngine& r){ /* GL calls, camera already set */ });
// Passes run in ascending 'order': 0 = background, 100 = world, 200 = effects.
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "core/render_engine/fov_rules.h"
#include "core/render_engine/render_scale.h"
#include "engine/module.h"
#include "engine/profiler.h"

namespace core {

struct Camera {
    float view[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; // column-major view matrix
    // fovDeg = the EFFECTIVE vertical field of view used this frame (what every pass and gluPerspective read); baseFovDeg = the setting (video.fov, vertical at 16:9).
    // RenderEngine derives fovDeg from baseFovDeg and the render aspect every frame (camera.fov_mode, see fov_rules.h).
    float fovDeg = 90.0f, baseFovDeg = 90.0f, nearZ = 0.1f, farZ = 20000.0f;
};

class RenderEngine : public engine::Module {
public:
    using Pass = std::function<void(RenderEngine&)>;

    const char* name() const override { return "core/render_engine"; }
    std::vector<std::string> dependencies() const override { return {"core/window"}; }
    int priority() const override { return -100; }
    bool required() const override { return true; }
    bool init(engine::Engine&) override;
    void shutdown(engine::Engine&) override;
    void onRender(engine::Engine&) override;

    void addPass(const std::string& name, int order, Pass fn);
    void removePass(const std::string& name);
    Camera camera;

    // Modules that draw every pixel of the view (the skybox) say so: the frame's colour clear is then skipped (render.clear_color = false).
    void setSceneCoversScreen(bool covers);
    float renderScale() const { return scaled_.scaled ? (float)scaled_.w / (float)std::max(1, window_w_) : 1.0f; }
    bool worldBufferActive() const { return scaled_.scaled; }

private:
    struct Entry { std::string name; int order; Pass fn; int profId = -1; };
    std::vector<std::shared_ptr<Entry>> passes_;
    std::vector<std::shared_ptr<Entry>> running_;   // scratch copy for onRender (a pass may add/remove passes): reused, no per-frame allocation
    class Window* window_ = nullptr;
    void updateClearPolicy();
    bool ensureWorldBuffer(int w, int h);      // (re)creates the offscreen buffer; false = fall back to the window
    void freeWorldBuffer();
    void compositeWorldBuffer(int windowW, int windowH);
    FovMode fovMode_ = FovMode::HorPlus;
    float scaleTunable_ = 1.0f;
    bool fboOk_ = false, clearTunable_ = true, sceneCovers_ = false;
    ScaledSize scaled_;
    int window_w_ = 1;
    unsigned fbo_ = 0, colorTex_ = 0, depthRb_ = 0;
    int fboW_ = 0, fboH_ = 0;
    int compositeId_ = -1;
    engine::Profiler* prof_ = nullptr;   // --profile: time every pass
};

} // namespace core
