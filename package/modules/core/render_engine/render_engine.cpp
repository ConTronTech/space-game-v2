#include "core/render_engine/render_engine.h"
#include <GL/gl.h>
#include <GL/glu.h>
#include <algorithm>
#include "core/window/window.h"
#include "engine/engine.h"

namespace core {

bool RenderEngine::init(engine::Engine& eng) {
    window_ = eng.services.get<Window>();
    if (!window_) return false;
    eng.services.provide<RenderEngine>(this);
    return true;
}

void RenderEngine::shutdown(engine::Engine& eng) { eng.services.withdraw<RenderEngine>(); }

void RenderEngine::addPass(const std::string& name, int order, Pass fn) {
    removePass(name);
    passes_.push_back({name, order, std::move(fn)});
    std::stable_sort(passes_.begin(), passes_.end(),
                     [](const Entry& a, const Entry& b) { return a.order < b.order; });
}

void RenderEngine::removePass(const std::string& name) {
    passes_.erase(std::remove_if(passes_.begin(), passes_.end(),
                                 [&](const Entry& e) { return e.name == name; }), passes_.end());
}

void RenderEngine::onRender(engine::Engine&) {
    int w = window_->width(), h = window_->height();
    glViewport(0, 0, w, h);
    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(camera.fovDeg, h > 0 ? (double)w / h : 1.0, camera.nearZ, camera.farZ);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(camera.view);

    auto passes = passes_; // a pass may add/remove passes while we iterate
    for (auto& p : passes) {
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        p.fn(*this);
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
    }
}

REGISTER_MODULE(RenderEngine);

} // namespace core
