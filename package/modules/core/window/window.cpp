#include "core/window/window.h"
#include "core/settings/settings_api.h"
#include <GL/gl.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "engine/engine.h"
#include "engine/json.h"
#include "engine/log.h"

namespace core {

namespace {

// Everything SDL knows about the screens, as plain data for display_rules.h.
std::vector<display::DisplayInfo> readSdlDisplays() {
    std::vector<display::DisplayInfo> out;
    int n = SDL_GetNumVideoDisplays();
    for (int i = 0; i < n; i++) {
        display::DisplayInfo d;
        d.index = i;
        const char* nm = SDL_GetDisplayName(i);
        d.name = nm && *nm ? nm : "display " + std::to_string(i);
        SDL_DisplayMode dm{};
        bool haveMode = SDL_GetDesktopDisplayMode(i, &dm) == 0;
        SDL_Rect r{0, 0, 0, 0};
        if (SDL_GetDisplayBounds(i, &r) == 0) { d.x = r.x; d.y = r.y; d.w = r.w; d.h = r.h; }
        else if (haveMode) { d.w = dm.w; d.h = dm.h; }
        if (haveMode) d.refreshHz = dm.refresh_rate;
        float diag = 0, hd = 0, vd = 0;
        if (SDL_GetDisplayDPI(i, &diag, &hd, &vd) == 0) { d.dpiX = hd; d.dpiY = vd; }
        int nm2 = SDL_GetNumDisplayModes(i);
        for (int j = 0; j < nm2; j++) {
            SDL_DisplayMode m{};
            if (SDL_GetDisplayMode(i, j, &m) == 0) d.modes.push_back({m.w, m.h, m.refresh_rate});
        }
        out.push_back(std::move(d));
    }
    return out;
}

engine::Json displaysJson(const std::vector<display::DisplayInfo>& list, int chosen) {
    engine::Json arr = engine::Json::array();
    for (const auto& d : list) {
        engine::Json modes = engine::Json::array();
        for (const auto& m : d.modes) modes.push(engine::Json::object().set("w", m.w).set("h", m.h).set("hz", m.hz));
        arr.push(engine::Json::object().set("index", d.index).set("name", d.name).set("x", d.x).set("y", d.y).set("w", d.w).set("h", d.h)
                     .set("refresh_hz", d.refreshHz).set("dpi_x", d.dpiX).set("dpi_y", d.dpiY).set("width_mm", d.widthMm()).set("height_mm", d.heightMm())
                     .set("aspect", display::aspectName(display::classifyAspect(d.w, d.h))).set("retro", display::isRetro(d)).set("fake", d.fake)
                     .set("chosen", d.index == chosen).set("modes", modes));
    }
    return engine::Json::object().set("displays", arr);
}

} // namespace

bool Window::init(engine::Engine& eng) {
    eng_ = &eng;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { LOG_E("window", "SDL_Init: %s", SDL_GetError()); return false; }
    auto* settings = eng.services.get<ISettings>();

    // ---- 1. the displays (real, or --fake-displays for tests) ----
    std::string fakeSpec = eng.flagValue("fake-displays");
    if (!fakeSpec.empty()) { displays_ = display::parseFakeDisplays(fakeSpec); fake_ = !displays_.empty(); }
    if (!fake_) displays_ = readSdlDisplays();

    // ---- 2. which one ----
    int mx = 0, my = 0;
    SDL_GetGlobalMouseState(&mx, &my);
    int flagDisplay = eng.flagValue("display").empty() ? -1 : std::atoi(eng.flagValue("display").c_str());
    std::string dispSetting = settings ? settings->get("video.display", std::string("auto")) : "auto";
    display::Choice choice = display::chooseDisplay(displays_, dispSetting, flagDisplay, !fake_, mx, my);
    chosen_ = choice.index;
    chosenWhy_ = choice.why;

    // ---- --list-displays[=json]: print and leave BEFORE any GL window exists (works over SSH with DISPLAY=:0) ----
    if (eng.hasFlag("list-displays") || !eng.flagValue("list-displays").empty()) {
        if (eng.flagValue("list-displays") == "json") std::fputs(displaysJson(displays_, chosen_).dump().c_str(), stdout);
        else {
            std::printf("%zu display(s)%s:\n", displays_.size(), fake_ ? " (FAKE, from --fake-displays)" : "");
            for (const auto& d : displays_) std::printf("  %s\n", display::describeDisplay(d, d.index == chosen_).c_str());
            std::printf("chosen: display %d (%s)\n", chosen_, chosenWhy_.c_str());
        }
        std::fflush(stdout);
        SDL_Quit();
        std::exit(0);
    }

    // ---- 3. video mode and sizes: flag > setting; the old switches keep working (--fullscreen, video.fullscreen = borderless on/off) ----
    display::VideoMode mode = display::VideoMode::Windowed;
    std::string modeText = eng.flagValue("video-mode");
    if (modeText.empty() && settings) modeText = settings->get("video.mode", std::string("auto"));
    if (!modeText.empty() && modeText != "auto") {
        if (!display::parseVideoMode(modeText, mode)) { LOG_W("window", "video mode '%s' is not borderless, exclusive or windowed: using the default", modeText.c_str()); mode = display::VideoMode::Windowed; }
    } else if ((settings && settings->get("video.fullscreen", false)) || eng.hasFlag("fullscreen")) mode = display::VideoMode::Borderless;

    const display::DisplayInfo* dinfo = chosen_ < (int)displays_.size() ? &displays_[(size_t)chosen_] : nullptr;
    display::Res flagRes;
    bool haveFlagRes = !eng.flagValue("resolution").empty() && display::parseResolution(eng.flagValue("resolution"), flagRes);
    if (!eng.flagValue("resolution").empty() && !haveFlagRes) LOG_W("window", "--resolution '%s' is not WxH or WxH@Hz: ignored", eng.flagValue("resolution").c_str());
    display::Res ws{1280, 720, 0};
    std::string wsText = settings ? settings->get("video.window_size", std::string()) : "";
    if (!wsText.empty()) { display::Res r; if (display::parseResolution(wsText, r)) ws = r; else LOG_W("window", "video.window_size '%s' is not WxH", wsText.c_str()); }
    else if (fake_ && dinfo) ws = {dinfo->w, dinfo->h, 0};                     // a fake display: open at ITS size (screenshot tests of 5:4, 4:3 ...)
    if (haveFlagRes && mode != display::VideoMode::Exclusive) ws = {flagRes.w, flagRes.h, 0};
    if (dinfo && !fake_) ws = display::fitWindow(*dinfo, ws);
    windowSize_ = ws;
    std::string resText = settings ? settings->get("video.resolution", std::string()) : "";
    display::Res er;
    if (haveFlagRes) exclusiveRes_ = flagRes;
    else if (!resText.empty() && display::parseResolution(resText, er)) exclusiveRes_ = er;
    mode_ = mode;
    w_ = ws.w; h_ = ws.h;

    // ---- 4. the window: on the chosen display, before the GL context ----
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    int realDisplay = fake_ ? 0 : chosen_;                                     // fake displays only shape sizes and logs: the window opens on a real one
    int posX = SDL_WINDOWPOS_CENTERED_DISPLAY(realDisplay), posY = posX;
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    if (mode == display::VideoMode::Borderless && !fake_) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    else if (mode == display::VideoMode::Exclusive && !fake_) flags |= SDL_WINDOW_HIDDEN;   // shown after its display mode is set
    win_ = SDL_CreateWindow("Space Game V2", posX, posY, w_, h_, flags);
    if (!win_) { LOG_E("window", "SDL_CreateWindow: %s", SDL_GetError()); return false; }
    if (mode == display::VideoMode::Exclusive && !fake_) { applyExclusive(); SDL_ShowWindow(win_); }
    gl_ = SDL_GL_CreateContext(win_);
    if (!gl_) { LOG_E("window", "SDL_GL_CreateContext: %s", SDL_GetError()); return false; }
    const int wantInterval = eng.hasFlag("no-vsync") ? 0 : 1;
    const int setResult = SDL_GL_SetSwapInterval(wantInterval);   // --no-vsync: measure what the machine can really do (see --benchmark)
    LOG_I("window", "swap interval: asked for %d, driver %s, now %d%s", wantInterval, setResult == 0 ? "accepted it" : "REFUSED it",
          SDL_GL_GetSwapInterval(), wantInterval == 0 && SDL_GL_GetSwapInterval() != 0 ? " (vsync is still on: the frame rate stays capped)" : "");
    SDL_GetWindowSize(win_, &w_, &h_);

    // Hardware log: lets the target laptop's real renderer / GL version / texture limit be confirmed from logs/game.log.
    {
        const char* ver = (const char*)glGetString(GL_VERSION);
        const char* ren = (const char*)glGetString(GL_RENDERER);
        const char* ven = (const char*)glGetString(GL_VENDOR);
        GLint maxTex = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
        LOG_I("window", "OpenGL %s | %s | %s | max texture %d", ver ? ver : "?", ven ? ven : "?", ren ? ren : "?", (int)maxTex);
        LOG_I("window", "display driver %s, window %dx%d, %d CPU threads, %d MB RAM", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?", w_, h_, SDL_GetCPUCount(), SDL_GetSystemRAM());
        int major = 0, minor = 0;
        if (ver && std::sscanf(ver, "%d.%d", &major, &minor) == 2 && (major < 2 || (major == 2 && minor < 1)))
            LOG_W("window", "OpenGL %d.%d is older than the 2.1 this game needs: rendering may be wrong", major, minor);
    }
    logBlock(eng, "startup");

    // live changes from the settings page (and anything else that writes these keys)
    eng.events.subscribe<SettingChanged>([this, &eng](const SettingChanged& e) {
        auto* s = eng.services.get<ISettings>();
        if (!s) return;
        if (e.key == "video.fullscreen") {
            bool on = s->get("video.fullscreen", false);
            if (on != (mode_ != display::VideoMode::Windowed)) applyMode(on ? display::VideoMode::Borderless : display::VideoMode::Windowed);   // already in step (the Mode row set both): nothing to do
        }
        else if (e.key == "video.mode") {
            display::VideoMode m;
            if (display::parseVideoMode(s->get("video.mode", std::string()), m)) applyMode(m);
        } else if (e.key == "video.resolution") {
            display::Res r;
            if (display::parseResolution(s->get("video.resolution", std::string()), r)) { exclusiveRes_ = r; if (mode_ == display::VideoMode::Exclusive) applyExclusive(); }
        } else if (e.key == "video.window_size") {
            display::Res r;
            if (display::parseResolution(s->get("video.window_size", std::string()), r)) {
                int di = current();
                windowSize_ = di < (int)displays_.size() && !fake_ ? display::fitWindow(displays_[(size_t)di], r) : r;
                if (mode_ == display::VideoMode::Windowed) { SDL_SetWindowSize(win_, windowSize_.w, windowSize_.h); if (di < (int)displays_.size() && !fake_) { auto p = display::centerOn(displays_[(size_t)di], windowSize_.w, windowSize_.h); SDL_SetWindowPosition(win_, p.x, p.y); } }
            }
        } else if (e.key == "video.display") {
            std::string v = s->get("video.display", std::string("auto"));
            display::Choice c = display::chooseDisplay(displays_, v, -1, true, 0, 0);
            if (v == "auto") c.index = current();
            moveToDisplay(c.index);
        }
    });
    shot_ = eng.flagValue("screenshot");
    shotFrame_ = std::atol(eng.flagValue("screenshot-frame", "30").c_str());
    eng.services.provide<Window>(this);
    eng.services.provide<IDisplays>(this);
    return true;
}

void Window::shutdown(engine::Engine& eng) {
    eng.services.withdraw<Window>();
    eng.services.withdraw<IDisplays>();
    if (gl_) SDL_GL_DeleteContext(gl_);
    if (win_) SDL_DestroyWindow(win_);
    SDL_Quit();
}

// ---- IDisplays ----
int Window::current() const {
    if (fake_ || !win_) return chosen_;
    int i = SDL_GetWindowDisplayIndex(win_);
    return i >= 0 && i < (int)displays_.size() ? i : chosen_;
}
void Window::drawableSize(int& w, int& h) const {
    w = w_; h = h_;
    if (win_ && gl_) SDL_GL_GetDrawableSize(win_, &w, &h);
}
int Window::refreshHz() const {
    int i = current();
    return i >= 0 && i < (int)displays_.size() ? displays_[(size_t)i].refreshHz : 0;
}
display::Res Window::currentResolution() const {
    if (mode_ == display::VideoMode::Exclusive && win_) {
        SDL_DisplayMode dm{};
        if (SDL_GetWindowDisplayMode(win_, &dm) == 0) return {dm.w, dm.h, dm.refresh_rate};
    }
    return {w_, h_, 0};
}

// ---- modes ----
// Exclusive fullscreen: the display mode closest to the wanted resolution (video.resolution / --resolution, default: the desktop mode).
void Window::applyExclusive() {
    int di = current();
    Uint32 keep = SDL_GetWindowFlags(win_) & SDL_WINDOW_HIDDEN;
    display::Res want = exclusiveRes_;
    if (want.w == 0 && di < (int)displays_.size()) want = {displays_[(size_t)di].w, displays_[(size_t)di].h, displays_[(size_t)di].refreshHz};
    display::Mode best{};
    bool exact = false;
    SDL_DisplayMode dm{};
    if (di < (int)displays_.size() && display::closestMode(displays_[(size_t)di].modes, want, best, &exact)) {
        dm.w = best.w; dm.h = best.h; dm.refresh_rate = best.hz; dm.format = 0; dm.driverdata = nullptr;
        SDL_SetWindowDisplayMode(win_, &dm);
        if (!exact) LOG_W("window", "no %s mode on this display: using the closest, %dx%d @ %d Hz", display::resolutionText(want).c_str(), best.w, best.h, best.hz);
    }
    (void)keep;
    if (SDL_SetWindowFullscreen(win_, SDL_WINDOW_FULLSCREEN) != 0) LOG_W("window", "exclusive fullscreen failed: %s", SDL_GetError());
    mode_ = display::VideoMode::Exclusive;
}

void Window::applyMode(display::VideoMode m) {
    if (!win_) return;
    int di = current();
    switch (m) {
        case display::VideoMode::Windowed:
            SDL_SetWindowFullscreen(win_, 0);
            SDL_SetWindowSize(win_, windowSize_.w, windowSize_.h);
            if (di >= 0 && di < (int)displays_.size() && !fake_) { auto p = display::centerOn(displays_[(size_t)di], windowSize_.w, windowSize_.h); SDL_SetWindowPosition(win_, p.x, p.y); }
            break;
        case display::VideoMode::Borderless:
            SDL_SetWindowFullscreen(win_, SDL_WINDOW_FULLSCREEN_DESKTOP);
            break;
        case display::VideoMode::Exclusive:
            applyExclusive();
            break;
    }
    mode_ = m;
    LOG_I("window", "video mode: %s", display::videoModeName(m));
}

void Window::moveToDisplay(int index) {
    if (!win_ || fake_ || index < 0 || index >= (int)displays_.size() || index == current()) return;
    display::VideoMode m = mode_;
    if (m != display::VideoMode::Windowed) SDL_SetWindowFullscreen(win_, 0);          // leave fullscreen, move, come back on the new display
    auto p = display::centerOn(displays_[(size_t)index], windowSize_.w, windowSize_.h);
    SDL_SetWindowSize(win_, windowSize_.w, windowSize_.h);
    SDL_SetWindowPosition(win_, p.x, p.y);
    if (m != display::VideoMode::Windowed) applyMode(m);
    LOG_I("window", "moved to display %d (%s)", index, displays_[(size_t)index].name.c_str());
}

// ---- the startup log block: ONE place that says everything about the screens ----
void Window::logBlock(engine::Engine&, const char* why) {
    LOG_I("display", "---- displays (%s): %zu found%s ----", why, displays_.size(), fake_ ? " (FAKE: --fake-displays)" : "");
    for (const auto& d : displays_) LOG_I("display", "%s", display::describeDisplay(d, d.index == chosen_).c_str());
    LOG_I("display", "chosen: display %d - %s", chosen_, chosenWhy_.c_str());
    int dw = 0, dh = 0;
    drawableSize(dw, dh);
    display::Res cur = currentResolution();
    LOG_I("display", "mode: %s, %dx%d%s (window %dx%d, drawable %dx%d, %s, swap interval %d, display refresh %d Hz)", display::videoModeName(mode_), cur.w, cur.h,
          mode_ == display::VideoMode::Exclusive && cur.hz ? (" @ " + std::to_string(cur.hz) + " Hz").c_str() : "", w_, h_, dw, dh,
          display::aspectName(display::classifyAspect(dw, dh)), SDL_GL_GetSwapInterval(), refreshHz());
    if (chosen_ < (int)displays_.size() && display::isRetro(displays_[(size_t)chosen_])) LOG_I("display", "hint: the chosen display looks like a CRT (retro): 4:3/5:4, >= 60 Hz, <= 1280 wide");
}

void Window::refreshDisplays(engine::Engine& eng, bool logIt) {
    if (fake_) return;
    displays_ = readSdlDisplays();
    if (chosen_ >= (int)displays_.size()) chosen_ = 0;
    if (logIt) logBlock(eng, "changed");
}

void Window::onFrameBegin(engine::Engine& eng) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) eng.events.emit(engine::QuitRequested{});
        if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            SDL_GetWindowSize(win_, &w_, &h_);
            eng.events.emit(WindowResized{w_, h_});
        }
#ifdef SDL_WINDOWEVENT_DISPLAY_CHANGED
        if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED) {   // dragged to another display, or a display was plugged / unplugged
            refreshDisplays(eng, true);
            int di = current();
            LOG_I("display", "the window is now on display %d", di);
            DisplayChanged dc;
            dc.index = di;
            if (di >= 0 && di < (int)displays_.size()) { dc.w = displays_[(size_t)di].w; dc.h = displays_[(size_t)di].h; }
            eng.events.emit(dc);
        }
#endif
        eng.events.emit(SdlEvent{e});
    }
    glClearColor(0.0f, 0.0f, 0.02f, 1.0f);
    glClear(colorClear_ ? (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT) : GL_DEPTH_BUFFER_BIT);
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
        if (s) { SDL_SaveBMP(s, shot.c_str()); SDL_FreeSurface(s); LOG_I("window", "saved %s", shot.c_str()); }
    }
    SDL_GL_SwapWindow(win_);
}

REGISTER_MODULE(Window);

} // namespace core
