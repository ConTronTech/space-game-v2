#pragma once
// core/window - owns the SDL window + OpenGL context and pumps OS events.
#include <SDL2/SDL.h>
#include <string>
#include "engine/module.h"

namespace core {

// Every raw SDL event is re-broadcast on the bus so other modules can react.
struct SdlEvent { const SDL_Event& e; };
struct WindowResized { int w, h; };

class Window : public engine::Module {
public:
    const char* name() const override { return "core/window"; }
    int priority() const override { return -1000; }
    bool required() const override { return true; }
    bool init(engine::Engine&) override;
    void shutdown(engine::Engine&) override;
    void onFrameBegin(engine::Engine&) override;
    void onPresent(engine::Engine&) override;

    int width() const { return w_; }
    int height() const { return h_; }
    SDL_Window* handle() const { return win_; }
    bool fullscreen() const { return (SDL_GetWindowFlags(win_) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0; }
    // The frame starts with a depth clear and (by default) a colour clear. Something that overwrites every pixel anyway may turn the colour clear off (see RenderEngine).
    void setColorClear(bool on) { colorClear_ = on; }
    bool colorClear() const { return colorClear_; }
    void setFullscreen(bool on) { SDL_SetWindowFullscreen(win_, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); }

private:
    SDL_Window* win_ = nullptr;
    SDL_GLContext gl_ = nullptr;
    int w_ = 1280, h_ = 720;
    std::string shot_;   // --screenshot=path
    long shotFrame_ = 30;
    bool colorClear_ = true;
};

} // namespace core
