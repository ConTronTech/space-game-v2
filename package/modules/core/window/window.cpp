#include "core/window/window.h"
#include <GL/gl.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "engine/engine.h"

namespace core {

bool Window::init(engine::Engine& eng) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return false; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    win_ = SDL_CreateWindow("Space Game V2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            w_, h_, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!win_) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false; }
    gl_ = SDL_GL_CreateContext(win_);
    if (!gl_) { std::fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError()); return false; }
    SDL_GL_SetSwapInterval(1);
    SDL_GetWindowSize(win_, &w_, &h_);

    shot_ = eng.flagValue("screenshot");
    shotFrame_ = std::atol(eng.flagValue("screenshot-frame", "30").c_str());
    eng.services.provide<Window>(this);
    return true;
}

void Window::shutdown(engine::Engine& eng) {
    eng.services.withdraw<Window>();
    if (gl_) SDL_GL_DeleteContext(gl_);
    if (win_) SDL_DestroyWindow(win_);
    SDL_Quit();
}

void Window::onFrameBegin(engine::Engine& eng) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) eng.events.emit(engine::QuitRequested{});
        if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            SDL_GetWindowSize(win_, &w_, &h_);
            eng.events.emit(WindowResized{w_, h_});
        }
        eng.events.emit(SdlEvent{e});
    }
    glClearColor(0.0f, 0.0f, 0.02f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

// Dev aid: --screenshot=out.bmp [--screenshot-frame=N] saves that frame and continues.
void Window::onPresent(engine::Engine& eng) {
    if (!shot_.empty() && (long)eng.frame() == shotFrame_) {
        const std::string& shot = shot_;
        std::vector<unsigned char> px((size_t)w_ * h_ * 4), flipped(px.size());
        glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        for (int y = 0; y < h_; y++)
            std::copy_n(&px[(size_t)y * w_ * 4], (size_t)w_ * 4, &flipped[(size_t)(h_ - 1 - y) * w_ * 4]);
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(flipped.data(), w_, h_, 32, w_ * 4, SDL_PIXELFORMAT_ABGR8888);
        if (s) { SDL_SaveBMP(s, shot.c_str()); SDL_FreeSurface(s); std::fprintf(stderr, "[window] saved %s\n", shot.c_str()); }
    }
    SDL_GL_SwapWindow(win_);
}

REGISTER_MODULE(Window);

} // namespace core
