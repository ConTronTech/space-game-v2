#pragma once
// core/render_engine - camera + ordered render passes.
// Modules don't draw from onRender(); they register a pass:
//     auto& r = eng.services.require<core::RenderEngine>();
//     r.addPass("my_stuff", 100, [](core::RenderEngine& r){ /* GL calls, camera already set */ });
// Passes run in ascending 'order': 0 = background, 100 = world, 200 = effects.
#include <functional>
#include <string>
#include <vector>
#include "engine/module.h"

namespace core {

struct Camera {
    float view[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; // column-major view matrix
    float fovDeg = 90.0f, nearZ = 0.1f, farZ = 20000.0f;
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

private:
    struct Entry { std::string name; int order; Pass fn; };
    std::vector<Entry> passes_;
    class Window* window_ = nullptr;
};

} // namespace core
