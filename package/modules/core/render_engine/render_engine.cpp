#include "core/render_engine/render_engine.h"
#include <GL/gl.h>
#include <GL/glu.h>
#include <algorithm>
#include "core/settings/settings_api.h"
#include "core/window/window.h"
#include "engine/engine.h"

namespace core {

bool RenderEngine::init(engine::Engine& eng) {
    window_ = eng.services.get<Window>();
    if (!window_) return false;
    if (auto* s = eng.services.get<ISettings>()) camera.fovDeg = std::clamp(s->get("video.fov", camera.fovDeg), 30.0f, 140.0f);
    eng.events.subscribe<SettingChanged>([this, &eng](const SettingChanged& e) {
        if (e.key != "video.fov") return;
        if (auto* s = eng.services.get<ISettings>()) camera.fovDeg = std::clamp(s->get("video.fov", camera.fovDeg), 30.0f, 140.0f);
    });
    prof_ = eng.services.get<engine::Profiler>();
    profGpu_ = prof_ && eng.flagValue("profile") == "gpu";
    eng.services.provide<RenderEngine>(this);
    return true;
}

void RenderEngine::shutdown(engine::Engine& eng) { eng.services.withdraw<RenderEngine>(); }

void RenderEngine::addPass(const std::string& name, int order, Pass fn) {
    removePass(name);
    passes_.push_back(std::make_shared<Entry>(Entry{name, order, std::move(fn), -1}));
    std::stable_sort(passes_.begin(), passes_.end(),
                     [](const auto& a, const auto& b) { return a->order < b->order; });
}

void RenderEngine::removePass(const std::string& name) {
    passes_.erase(std::remove_if(passes_.begin(), passes_.end(),
                                 [&](const auto& e) { return e->name == name; }), passes_.end());
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

    running_ = passes_; // a pass may add/remove passes while we iterate (shared_ptr copies: cheap, capacity reused)
    for (auto& pp : running_) {
        Entry& p = *pp;
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        if (prof_) {
            if (p.profId < 0) p.profId = prof_->intern("pass:" + p.name);
            if (profGpu_) glFinish();                       // earlier GPU work must not be charged to this pass
            engine::ProfScope scope(prof_, p.profId);
            p.fn(*this);
            if (profGpu_) glFinish();
        } else {
            p.fn(*this);
        }
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
    }
}

REGISTER_MODULE(RenderEngine);

} // namespace core
