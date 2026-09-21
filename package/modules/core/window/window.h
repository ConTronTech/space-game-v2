#pragma once
// core/window - owns the SDL window + OpenGL context and pumps OS events. Display-aware: see window_api.h (IDisplays) and docs/DISPLAYS.md.
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include "core/window/window_api.h"
#include "engine/module.h"

namespace core {

// Every raw SDL event is re-broadcast on the bus so other modules can react.
struct SdlEvent { const SDL_Event& e; };
struct WindowResized { int w, h; };

class Window : public engine::Module, public IDisplays {
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
    bool fullscreen() const { return win_ && (SDL_GetWindowFlags(win_) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0; }   // borderless or exclusive
    // The old switch, kept: true = borderless desktop fullscreen (SDL_WINDOW_FULLSCREEN_DESKTOP), false = windowed.
    void setFullscreen(bool on) { applyMode(on ? display::VideoMode::Borderless : display::VideoMode::Windowed); }
    display::VideoMode videoMode() const { return mode_; }

    // ---- live changes (also driven by the settings video.mode / video.resolution / video.window_size / video.display) ----
    void applyMode(display::VideoMode m);
    void moveToDisplay(int index);                   // moves the window (and its fullscreen state) to another display
    display::Res currentResolution() const;          // the size in use: the window size (windowed) or the exclusive mode

    // ---- IDisplays ----
    const std::vector<display::DisplayInfo>& list() const override { return displays_; }
    int chosen() const override { return chosen_; }
    int current() const override;
    void drawableSize(int& w, int& h) const override;
    int refreshHz() const override;
    display::Aspect aspect() const override { return display::classifyAspect(w_, h_); }

private:
    void refreshDisplays(engine::Engine& eng, bool logIt);
    void logBlock(engine::Engine& eng, const char* why);
    void applyExclusive();
    SDL_Window* win_ = nullptr;
    SDL_GLContext gl_ = nullptr;
    int w_ = 1280, h_ = 720;
    std::string shot_;   // --screenshot=path
    long shotFrame_ = 30;
    bool colorClear_ = true;
    engine::Engine* eng_ = nullptr;
    std::vector<display::DisplayInfo> displays_;
    bool fake_ = false;
    int chosen_ = 0;
    std::string chosenWhy_;
    display::VideoMode mode_ = display::VideoMode::Windowed;
    display::Res windowSize_{1280, 720, 0}, exclusiveRes_{0, 0, 0};

public:
    // The frame starts with a depth clear and (by default) a colour clear. Something that overwrites every pixel anyway may turn the colour clear off (see RenderEngine).
    void setColorClear(bool on) { colorClear_ = on; }
    bool colorClear() const { return colorClear_; }
};

} // namespace core
