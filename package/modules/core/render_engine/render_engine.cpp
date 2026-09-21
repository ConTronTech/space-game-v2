#include "core/render_engine/render_engine.h"
#include <GL/gl.h>
#include <GL/glu.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/glext.h>
#include <SDL2/SDL.h>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "core/settings/settings_api.h"
#include "core/window/window.h"
#include "engine/engine.h"
#include "engine/log.h"

namespace core {

namespace {
// EXT_framebuffer_object entry points (Mesa on Ironlake has the extension; looked up by name so nothing depends on link-time symbols).
struct FboApi {
    PFNGLGENFRAMEBUFFERSEXTPROC gen = nullptr;
    PFNGLBINDFRAMEBUFFEREXTPROC bind = nullptr;
    PFNGLFRAMEBUFFERTEXTURE2DEXTPROC tex2d = nullptr;
    PFNGLGENRENDERBUFFERSEXTPROC genRb = nullptr;
    PFNGLBINDRENDERBUFFEREXTPROC bindRb = nullptr;
    PFNGLRENDERBUFFERSTORAGEEXTPROC storageRb = nullptr;
    PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC fbRb = nullptr;
    PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC check = nullptr;
    PFNGLDELETEFRAMEBUFFERSEXTPROC del = nullptr;
    PFNGLDELETERENDERBUFFERSEXTPROC delRb = nullptr;
    bool ok() const { return gen && bind && tex2d && genRb && bindRb && storageRb && fbRb && check && del && delRb; }
};
FboApi g_fbo;

bool loadFboApi() {
    const char* ext = (const char*)glGetString(GL_EXTENSIONS);
    if (!ext || !std::strstr(ext, "GL_EXT_framebuffer_object")) return false;
    g_fbo.gen = (PFNGLGENFRAMEBUFFERSEXTPROC)SDL_GL_GetProcAddress("glGenFramebuffersEXT");
    g_fbo.bind = (PFNGLBINDFRAMEBUFFEREXTPROC)SDL_GL_GetProcAddress("glBindFramebufferEXT");
    g_fbo.tex2d = (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)SDL_GL_GetProcAddress("glFramebufferTexture2DEXT");
    g_fbo.genRb = (PFNGLGENRENDERBUFFERSEXTPROC)SDL_GL_GetProcAddress("glGenRenderbuffersEXT");
    g_fbo.bindRb = (PFNGLBINDRENDERBUFFEREXTPROC)SDL_GL_GetProcAddress("glBindRenderbufferEXT");
    g_fbo.storageRb = (PFNGLRENDERBUFFERSTORAGEEXTPROC)SDL_GL_GetProcAddress("glRenderbufferStorageEXT");
    g_fbo.fbRb = (PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC)SDL_GL_GetProcAddress("glFramebufferRenderbufferEXT");
    g_fbo.check = (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)SDL_GL_GetProcAddress("glCheckFramebufferStatusEXT");
    g_fbo.del = (PFNGLDELETEFRAMEBUFFERSEXTPROC)SDL_GL_GetProcAddress("glDeleteFramebuffersEXT");
    g_fbo.delRb = (PFNGLDELETERENDERBUFFERSEXTPROC)SDL_GL_GetProcAddress("glDeleteRenderbuffersEXT");
    return g_fbo.ok();
}
} // namespace

bool RenderEngine::init(engine::Engine& eng) {
    window_ = eng.services.get<Window>();
    if (!window_) return false;
    if (auto* s = eng.services.get<ISettings>()) camera.baseFovDeg = std::clamp(s->get("video.fov", camera.baseFovDeg), 30.0f, 140.0f);
    camera.fovDeg = camera.baseFovDeg;
    fovMode_ = parseFovMode(eng.config.get<std::string>("camera.fov_mode", "horplus", "vertical | horplus. video.fov is a VERTICAL fov at 16:9; horplus keeps that view's HORIZONTAL fov on narrower screens (4:3, 5:4: nothing cropped) and caps it on ultrawide; vertical uses it as is at every aspect (docs/DISPLAYS.md)"));
    eng.events.subscribe<SettingChanged>([this, &eng](const SettingChanged& e) {
        if (e.key != "video.fov") return;
        if (auto* s = eng.services.get<ISettings>()) camera.baseFovDeg = std::clamp(s->get("video.fov", camera.baseFovDeg), 30.0f, 140.0f);
    });
    // render.scale: draw the 3D world at a fraction of the window size and stretch it (the UI stays native). Needs framebuffer objects.
    scaleTunable_ = clampRenderScale(eng.config.get("render.scale", 0.85f, "render the 3D world at this fraction of the window size (0.5 - 1.0) and stretch it; the UI stays sharp. 1.0 = off. Presets: low 0.7, medium 0.85, high/ultra 1.0"));
    clearTunable_ = eng.config.get("render.clear_color", true, "clear the colour buffer every frame. false lets the skybox / the stretched world buffer overwrite every pixel instead (cheaper). Presets: low false");
    fboOk_ = scaleTunable_ < 0.995f && loadFboApi();
    if (scaleTunable_ < 0.995f && !fboOk_) LOG_W("render", "render.scale %.2f needs GL_EXT_framebuffer_object, which this driver does not have: rendering at full size", scaleTunable_);
    window_w_ = window_->width();
    updateClearPolicy();
    if (fboOk_) LOG_I("render", "render.scale %.2f: the world is drawn offscreen at %d%% of the window size and stretched", scaleTunable_, (int)std::lround(scaleTunable_ * 100));
    else LOG_I("render", "render.scale 1.0 (off): the world is drawn straight to the window");
    prof_ = eng.services.get<engine::Profiler>();
    if (prof_) { char b[96]; std::snprintf(b, sizeof b, "render.scale %.2f%s, render.clear_color %s", scaleTunable_, fboOk_ ? "" : " (off)", clearTunable_ ? "true" : "false"); prof_->setNote(b); }
    eng.services.provide<RenderEngine>(this);
    return true;
}

void RenderEngine::shutdown(engine::Engine& eng) {
    freeWorldBuffer();
    if (window_) window_->setColorClear(true);
    eng.services.withdraw<RenderEngine>();
}

void RenderEngine::setSceneCoversScreen(bool covers) { sceneCovers_ = covers; updateClearPolicy(); }

void RenderEngine::updateClearPolicy() {
    if (window_) window_->setColorClear(colorClearNeeded(clearTunable_, sceneCovers_, fboOk_));
}

void RenderEngine::freeWorldBuffer() {
    if (!fbo_ && !colorTex_ && !depthRb_) return;
    if (fbo_) g_fbo.del(1, &fbo_);
    if (depthRb_) g_fbo.delRb(1, &depthRb_);
    if (colorTex_) glDeleteTextures(1, &colorTex_);
    fbo_ = colorTex_ = depthRb_ = 0;
    fboW_ = fboH_ = 0;
}

bool RenderEngine::ensureWorldBuffer(int w, int h) {
    if (fbo_ && fboW_ == w && fboH_ == h) return true;
    freeWorldBuffer();
    glGenTextures(1, &colorTex_);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    g_fbo.gen(1, &fbo_);
    g_fbo.bind(GL_FRAMEBUFFER_EXT, fbo_);
    g_fbo.tex2d(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, colorTex_, 0);
    g_fbo.genRb(1, &depthRb_);
    g_fbo.bindRb(GL_RENDERBUFFER_EXT, depthRb_);
    g_fbo.storageRb(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT24, w, h);
    g_fbo.fbRb(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, depthRb_);
    GLenum st = g_fbo.check(GL_FRAMEBUFFER_EXT);
    g_fbo.bind(GL_FRAMEBUFFER_EXT, 0);
    if (st != GL_FRAMEBUFFER_COMPLETE_EXT) {
        LOG_W("render", "the %dx%d world buffer is incomplete (status 0x%x): rendering at full size from now on", w, h, (unsigned)st);
        freeWorldBuffer();
        fboOk_ = false;
        updateClearPolicy();
        return false;
    }
    fboW_ = w; fboH_ = h;
    LOG_I("render", "world buffer %dx%d (window %dx%d)", w, h, window_->width(), window_->height());
    return true;
}

// Stretches the world buffer over the whole window: one textured quad, no depth, no blend.
void RenderEngine::compositeWorldBuffer(int windowW, int windowH) {
    g_fbo.bind(GL_FRAMEBUFFER_EXT, 0);
    glViewport(0, 0, windowW, windowH);
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f(1, -1);
    glTexCoord2f(1, 1); glVertex2f(1, 1);
    glTexCoord2f(0, 1); glVertex2f(-1, 1);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();
}

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
    window_w_ = w;
    scaled_ = scaledSize(w, h, scaleTunable_, fboOk_);
    if (scaled_.scaled && !ensureWorldBuffer(scaled_.w, scaled_.h)) scaled_ = {w, h, false};
    int rw = scaled_.w, rh = scaled_.h;
    if (scaled_.scaled) {
        g_fbo.bind(GL_FRAMEBUFFER_EXT, fbo_);
        glDisable(GL_SCISSOR_TEST);
        glDepthMask(GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.02f, 1.0f);
        glClear(colorClearNeeded(clearTunable_, sceneCovers_, false) ? (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT) : GL_DEPTH_BUFFER_BIT);
    }
    glViewport(0, 0, rw, rh);
    glEnable(GL_DEPTH_TEST);
    camera.fovDeg = effectiveVerticalFov(fovMode_, camera.baseFovDeg, rh > 0 ? (float)rw / (float)rh : 1.0f);   // aspect-aware: 5:4 and 4:3 keep the 16:9 view's horizontal extent

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(camera.fovDeg, rh > 0 ? (double)rw / rh : 1.0, camera.nearZ, camera.farZ);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(camera.view);

    const bool gpu = prof_ && prof_->gpuMode();     // --profile=gpu, or F4 in the live overlay: glFinish around every pass
    running_ = passes_; // a pass may add/remove passes while we iterate (shared_ptr copies: cheap, capacity reused)
    for (auto& pp : running_) {
        Entry& p = *pp;
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        if (prof_) {
            if (p.profId < 0) p.profId = prof_->intern("pass:" + p.name);
            if (gpu) glFinish();                            // earlier GPU work must not be charged to this pass
            engine::ProfScope scope(prof_, p.profId);
            p.fn(*this);
            if (gpu) glFinish();
        } else {
            p.fn(*this);
        }
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
    }
    if (scaled_.scaled) {
        engine::ProfScope scope(prof_, prof_ ? (compositeId_ < 0 ? (compositeId_ = prof_->intern("pass:render.scale composite")) : compositeId_) : -1);
        if (gpu) glFinish();
        compositeWorldBuffer(w, h);
        if (gpu) glFinish();
    }
}

REGISTER_MODULE(RenderEngine);

} // namespace core
