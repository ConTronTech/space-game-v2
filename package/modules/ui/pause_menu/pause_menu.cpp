// ui/pause_menu - Esc pauses the simulation and opens a glass menu.
//   Main:     Resume / Save Game / Load Game / Settings / Exit Game   (Save/Load only if core/save_system is loaded)
//   Load:     your saves, newest first
//   Settings: Field of View, Mouse Sensitivity, Master/Effects/Engine Volume (if core/audio is loaded), Fullscreen, Display (2+ displays), Mode, Resolution, Graphics, Back
//             (the page scales with the window and switches to two columns when it does not fit: 5:4 / 4:3 / small windows)
// Mouse or keyboard/controller (actions: pause, ui_up, ui_down, ui_left, ui_right, ui_confirm).
// Settings go through core::ISettings; saving/loading goes through core::ISaveSystem.
#include <algorithm>
#include <cmath>
#include <ctime>
#include "core/audio/audio_api.h"
#include "core/input_handler/input_api.h"
#include "core/quality/quality_api.h"
#include "core/quality/quality_rules.h"
#include "core/render_engine/render_engine.h"
#include "core/save_system/save_api.h"
#include "core/settings/settings_api.h"
#include "core/ui_handler/ui_handler.h"
#include "core/window/window.h"
#include "engine/engine.h"

class PauseMenu : public engine::Module {
public:
    const char* name() const override { return "ui/pause_menu"; }
    std::vector<std::string> dependencies() const override {
        return {"core/input_handler", "core/ui_handler", "core/render_engine", "core/window"};
    }

    bool init(engine::Engine& eng) override {
        eng_ = &eng;
        input_ = &eng.services.require<core::IInput>();
        render_ = &eng.services.require<core::RenderEngine>();
        window_ = &eng.services.require<core::Window>();
        ui_ = &eng.services.require<core::UIHandler>();
        settings_ = eng.services.get<core::ISettings>();     // optional: without it the settings page is read-only
        saves_ = eng.services.get<core::ISaveSystem>();      // optional: without it Save/Load are hidden
        audio_ = eng.services.get<core::IAudio>();           // optional: without it no click sounds / volume sliders

        rows_ = {Row::Fov, Row::Sens};
        if (audio_) { rows_.push_back(Row::Master); rows_.push_back(Row::Sfx); rows_.push_back(Row::EngineVol); }
        rows_.push_back(Row::Fullscreen);
        if (window_->list().size() > 1) rows_.push_back(Row::Display);      // only when there is a choice
        rows_.push_back(Row::Mode);
        rows_.push_back(Row::Resolution);
        quality_ = eng.services.get<core::IQuality>();       // optional: without it (or without settings) there is no Graphics row
        if (quality_ && settings_) rows_.push_back(Row::Graphics);
        rows_.push_back(Row::Back);

        main_ = {Main::Resume};
        if (saves_) { main_.push_back(Main::Save); main_.push_back(Main::Load); }
        main_.push_back(Main::Settings);
        main_.push_back(Main::Exit);

        ui_->addPanel("ui/pause_menu", 1000, [this](core::UIHandler& ui) { if (eng_->paused()) draw(ui); });
        // dev aid: --paused (or --paused=settings|load) starts with the menu open
        if (eng.hasFlag("paused") || !eng.flagValue("paused").empty()) eng.setPaused(true);
        if (eng.flagValue("paused") == "settings") page_ = Page::Settings;
        if (eng.flagValue("paused") == "load" && saves_) openLoad();
        return true;
    }

    void shutdown(engine::Engine&) override { ui_->removePanel("ui/pause_menu"); }

    void onUpdate(engine::Engine& eng, float) override {
        if (input_->pressed("pause")) {
            if (!eng.paused())         { page_ = Page::Main; focus_ = 0; status_.clear(); eng.setPaused(true); }
            else if (page_ != Page::Main) backToMain();
            else                       eng.setPaused(false);
            return;
        }
        if (!eng.paused()) return;

        int n = itemCount();
        int before = focus_;
        if (input_->pressed("ui_down")) focus_ = (focus_ + 1) % n;
        if (input_->pressed("ui_up"))   focus_ = (focus_ + n - 1) % n;
        if (focus_ != before) sound("ui_click", 0.5f);
        if (input_->pressed("ui_confirm")) activate(focus_);
        if (input_->pressed("ui_left"))  adjust(focus_, -1);
        if (input_->pressed("ui_right")) adjust(focus_, +1);
    }

private:
    enum class Page { Main, Load, Settings };
    enum class Main { Resume, Save, Load, Settings, Exit };
    enum class Row { Fov, Sens, Master, Sfx, EngineVol, Fullscreen, Display, Mode, Resolution, Graphics, Back };   // settings rows (built in init)
    static constexpr float kFovMin = 60, kFovMax = 120, kSensMin = 0.2f, kSensMax = 3.0f;
    static constexpr size_t kMaxSlotsShown = 6;

    int itemCount() const {
        switch (page_) {
            case Page::Main: return (int)main_.size();
            case Page::Load: return (int)slots_.size() + 1;      // slots..., Back
            default: return (int)rows_.size();
        }
    }
    int mainIndexOf(Main m) const { for (size_t i = 0; i < main_.size(); i++) if (main_[i] == m) return (int)i; return 0; }
    float mouseSens() const { return settings_ ? settings_->get("input.mouse_sensitivity", 1.0f) : 1.0f; }

    void setFov(float v) { if (settings_) settings_->set("video.fov", std::clamp(v, kFovMin, kFovMax)); }
    void setSens(float v) { if (settings_) settings_->set("input.mouse_sensitivity", std::clamp(v, kSensMin, kSensMax)); }
    void setFullscreen(bool on) {
        if (!settings_) return;
        settings_->set("video.mode", std::string(on ? "borderless" : "windowed"));   // keep video.mode in step: it wins at the next start
        settings_->set("video.fullscreen", on);
    }
    void setVolume(const char* key, float v) { if (settings_) settings_->set(key, std::clamp(v, 0.0f, 100.0f)); }
    void sound(const char* name, float vol) { if (audio_) audio_->play(name, vol); }

    void backToMain() {
        Page from = page_;
        page_ = Page::Main;
        focus_ = mainIndexOf(from == Page::Load ? Main::Load : Main::Settings);
        status_.clear();
    }
    void openLoad() {
        slots_ = saves_->listSlots();
        if (slots_.size() > kMaxSlotsShown) slots_.resize(kMaxSlotsShown);
        page_ = Page::Load;
        focus_ = 0;
        status_.clear();
    }
    void setStatus(const std::string& s, bool ok) { status_ = s; statusOk_ = ok; }

    // Graphics preset: the choice is saved as settings "quality.preset" and applies on the next launch (core/quality reads it at startup).
    quality::Preset pendingQuality() const {
        return quality::parsePreset(settings_->get("quality.preset", quality_->selectedName())).preset;
    }
    void cycleQuality(int dir) {
        quality::Preset next = quality::cyclePreset(pendingQuality(), dir);
        settings_->set("quality.preset", std::string(quality::presetName(next)));
    }
    std::string qualityLabel() const {
        quality::Preset p = pendingQuality();
        std::string name = quality::titleName(p);
        if (p == quality::Preset::Auto) name += " (" + quality::titleName(quality::parsePreset(quality_->detectedName()).preset) + ")";
        return "Graphics: " + name + "  (restart)";
    }

    // ---- display / mode / resolution (live: core/window reacts to the settings) ----
    // Numbers shown to the player are 1-based ("Display 2"); the setting video.display and --display use the 0-based SDL index.
    std::string displaySetting() const { return settings_ ? settings_->get("video.display", std::string("auto")) : "auto"; }
    // "Display: 2 (VGA-1 1280x1024)"; when that is wider than the button it drops the size, then truncates the name.
    std::string displayLabel(core::UIHandler& ui, int fontSize, float maxWidth) const {
        const auto& list = window_->list();
        std::string s = displaySetting();
        int idx = s == "auto" ? window_->current() : std::atoi(s.c_str());
        std::string head = std::string("Display: ") + (s == "auto" ? "Auto" : std::to_string(idx + 1));
        if (idx < 0 || idx >= (int)list.size()) return head;
        const auto& d = list[(size_t)idx];
        std::string full = head + " (" + d.name + " " + std::to_string(d.w) + "x" + std::to_string(d.h) + ")";
        if (ui.textWidth(full, fontSize) <= maxWidth) return full;
        std::string name = d.name;
        for (;;) {
            std::string t = head + " (" + name + ")";
            if (ui.textWidth(t, fontSize) <= maxWidth || name.size() <= 3) return t;
            name = name.substr(0, name.size() - 2) + "~";   // drop a character before the "~"
            if (name.size() >= 2 && name[name.size() - 2] == '~') name.erase(name.size() - 2, 1);
        }
    }
    void cycleDisplay(int dir) {
        int n = (int)window_->list().size();
        if (n < 2 || !settings_) return;
        std::string s = displaySetting();
        int pos = s == "auto" ? 0 : std::clamp(std::atoi(s.c_str()), 0, n - 1) + 1;   // 0 = Auto, 1..n = displays
        pos = (pos + dir + n + 1) % (n + 1);
        settings_->set("video.display", pos == 0 ? std::string("auto") : std::to_string(pos - 1));
    }
    static const char* modeTitle(core::display::VideoMode m) { return m == core::display::VideoMode::Borderless ? "Borderless" : m == core::display::VideoMode::Exclusive ? "Exclusive" : "Windowed"; }
    void cycleMode(int dir) {
        if (!settings_) return;
        using VM = core::display::VideoMode;
        static const VM order[3] = {VM::Windowed, VM::Borderless, VM::Exclusive};
        int pos = 0;
        for (int i = 0; i < 3; i++) if (order[i] == window_->videoMode()) pos = i;
        VM next = order[(pos + dir + 3) % 3];
        settings_->set("video.mode", std::string(core::display::videoModeName(next)));   // core/window applies it now
        settings_->set("video.fullscreen", next != VM::Windowed);                        // keep the old switch in step (the window sees it already matches)
    }
    std::vector<core::display::Res> resolutions() const {
        const auto& list = window_->list();
        int di = window_->current();
        if (di < 0 || di >= (int)list.size()) return {};
        return core::display::selectableResolutions(list[(size_t)di].modes);
    }
    std::string resolutionLabel() const {
        using VM = core::display::VideoMode;
        if (window_->videoMode() == VM::Borderless) return "Resolution: desktop";
        core::display::Res r = window_->currentResolution();
        return "Resolution: " + core::display::resolutionText({r.w, r.h, window_->videoMode() == VM::Exclusive ? r.hz : 0});
    }
    void cycleResolution(int dir) {
        using VM = core::display::VideoMode;
        if (!settings_ || window_->videoMode() == VM::Borderless) return;
        auto list = resolutions();
        if (list.empty()) return;
        core::display::Res cur = window_->currentResolution();
        int pos = 0;
        for (size_t i = 0; i < list.size(); i++) if (list[i].w == cur.w && list[i].h == cur.h) pos = (int)i;
        core::display::Res next = list[(size_t)((pos + dir + (int)list.size()) % (int)list.size())];
        if (window_->videoMode() == VM::Exclusive) settings_->set("video.resolution", core::display::resolutionText(next));
        else settings_->set("video.window_size", core::display::resolutionText({next.w, next.h, 0}));
    }

    void activate(int i) {
        sound("ui_confirm", 0.8f);
        if (page_ == Page::Main) {
            switch (main_[(size_t)i]) {
                case Main::Resume:   eng_->setPaused(false); break;
                case Main::Save: {
                    std::string slot = saves_->newSlotName();
                    if (saves_->saveSlot(slot)) setStatus("Game saved", true);
                    else setStatus("Save failed - see logs/game.log", false);
                    break;
                }
                case Main::Load:     openLoad(); break;
                case Main::Settings: page_ = Page::Settings; focus_ = 0; status_.clear(); break;
                case Main::Exit:     eng_->quit(); break;
            }
        } else if (page_ == Page::Load) {
            if (i == (int)slots_.size()) backToMain();
            else if (saves_->loadSlot(slots_[(size_t)i].name)) eng_->setPaused(false);
            else setStatus("Could not load that save - see logs/game.log", false);
        } else {
            Row r = rows_[(size_t)i];
            if (r == Row::Fullscreen) setFullscreen(!window_->fullscreen());
            else if (r == Row::Graphics) cycleQuality(1);
            else if (r == Row::Display) cycleDisplay(1);
            else if (r == Row::Mode) cycleMode(1);
            else if (r == Row::Resolution) cycleResolution(1);
            else if (r == Row::Back) backToMain();
        }
    }
    void adjust(int i, int dir) {
        if (page_ != Page::Settings) return;
        switch (rows_[(size_t)i]) {
            case Row::Graphics:  cycleQuality(dir); break;
            case Row::Display:   cycleDisplay(dir); break;
            case Row::Mode:      cycleMode(dir); break;
            case Row::Resolution: cycleResolution(dir); break;
            case Row::Fov:       setFov(render_->camera.baseFovDeg + 5.0f * dir); break;
            case Row::Sens:      setSens(mouseSens() + 0.1f * dir); break;
            case Row::Master:    setVolume("audio.master", audio_->masterVolume() + 5.0f * dir); break;
            case Row::Sfx:       setVolume("audio.sfx", audio_->busVolume(core::Bus::Sfx) + 5.0f * dir); break;
            case Row::EngineVol: setVolume("audio.engine", audio_->busVolume(core::Bus::Engine) + 5.0f * dir); break;
            default: break;
        }
    }

    static std::string when(long long t) {
        std::time_t tt = (std::time_t)t;
        char b[32];
        std::strftime(b, sizeof b, "%Y-%m-%d  %H:%M:%S", std::localtime(&tt));
        return b;
    }

    void draw(core::UIHandler& ui) {
        const float W = (float)ui.width(), H = (float)ui.height();
        ui.rect(0, 0, W, H, 0.0f, 0.02f, 0.05f, 0.45f); // dim the game behind the glass

        const int n = itemCount();
        const bool emptyLoad = page_ == Page::Load && slots_.empty();
        const int rows = n + (emptyLoad ? 1 : 0);           // an extra text row explains an empty list
        // Everything scales with the window (ui.scale: 1.0 at 1280x720). A page that does not fit vertically (the settings page has up to 12 rows) uses two columns.
        const float sc = ui.scale;
        auto fs = [&](int px) { return std::max(9, (int)std::lround(px * sc)); };
        const float pw = 400 * sc, itemH = 52 * sc, gap = 10 * sc, pad = 28 * sc, head = 84 * sc, foot = status_.empty() ? 0.0f : 26 * sc, colGap = 16 * sc;
        const float avail = H - 24 * sc;
        int perCol = std::max(1, (int)((avail - head - pad - foot + gap) / (itemH + gap)));
        int cols = std::min(2, (rows + perCol - 1) / perCol);
        if (cols < 1) cols = 1;
        perCol = (rows + cols - 1) / cols;                  // balance the columns
        float ph = head + perCol * itemH + (perCol - 1) * gap + pad + foot;
        float totalW = cols * pw + (cols - 1) * colGap;
        float px = (W - totalW) / 2, py = (H - ph) / 2;
        ui.glass(px, py, totalW, ph, 1.0f, false, 18 * sc);

        const char* title = page_ == Page::Main ? "PAUSED" : page_ == Page::Load ? "LOAD GAME" : "SETTINGS";
        ui.textCentered(W / 2, py + 22 * sc, title, fs(28), ui.theme.text);
        ui.textCentered(W / 2, py + 58 * sc, page_ == Page::Main ? "Esc to resume" : "Esc to go back", fs(13), ui.theme.textDim);

        // hovering an item moves focus, but only when the mouse actually moved, so keyboard nav still works
        bool moved = ui.mouseX() != lastMx_ || ui.mouseY() != lastMy_;
        lastMx_ = ui.mouseX(); lastMy_ = ui.mouseY();

        float iw = pw - pad * 2;
        auto slotOf = [&](int r) { return r; };
        auto rowY = [&](int r) { return py + head + (slotOf(r) % perCol) * (itemH + gap); };
        auto colX = [&](int r) { return px + (slotOf(r) / perCol) * (pw + colGap) + pad; };
        if (emptyLoad) ui.textCentered(W / 2, rowY(0) + 16 * sc, "No saved games yet", fs(16), ui.theme.textDim);

        // A click must NOT change the page/lists while we are still drawing rows of the old page (the loop would then index the new
        // page's arrays with the old page's row numbers: out-of-bounds). Record the click and act on it after the loop.
        int pending = -1;
        for (int i = 0; i < n; i++) {
            float iy = rowY(i + (emptyLoad ? 1 : 0)), ix = colX(i + (emptyLoad ? 1 : 0));
            if (moved && ui.hovered(ix, iy, iw, itemH) && focus_ != i) { focus_ = i; sound("ui_click", 0.5f); }
            bool f = focus_ == i;

            if (page_ == Page::Main) {
                static const char* labels[] = {"Resume", "Save Game", "Load Game", "Settings", "Exit Game"};
                if (ui.button(labels[(int)main_[(size_t)i]], ix, iy, iw, itemH, f)) pending = i;
            } else if (page_ == Page::Load) {
                if (i == (int)slots_.size()) { if (ui.button("Back", ix, iy, iw, itemH, f)) pending = i; }
                else if (ui.button(when(slots_[(size_t)i].time), ix, iy, iw, itemH, f)) pending = i;
            } else {
                switch (rows_[(size_t)i]) {
                    case Row::Fov: {
                        float cur = render_->camera.baseFovDeg;
                        float v = ui.slider("Field of View", ix, iy, iw, itemH, cur, kFovMin, kFovMax, f);
                        if (v != cur) setFov(v);
                        break;
                    }
                    case Row::Sens: {
                        float cur = mouseSens();
                        float v = ui.slider("Mouse Sensitivity", ix, iy, iw, itemH, cur, kSensMin, kSensMax, f, "%.2f");
                        if (v != cur) setSens(v);
                        break;
                    }
                    case Row::Master: {
                        float cur = audio_->masterVolume();
                        float v = ui.slider("Master Volume", ix, iy, iw, itemH, cur, 0, 100, f);
                        if (v != cur) setVolume("audio.master", v);
                        break;
                    }
                    case Row::Sfx: {
                        float cur = audio_->busVolume(core::Bus::Sfx);
                        float v = ui.slider("Effects Volume", ix, iy, iw, itemH, cur, 0, 100, f);
                        if (v != cur) setVolume("audio.sfx", v);
                        break;
                    }
                    case Row::EngineVol: {
                        float cur = audio_->busVolume(core::Bus::Engine);
                        float v = ui.slider("Engine Volume", ix, iy, iw, itemH, cur, 0, 100, f);
                        if (v != cur) setVolume("audio.engine", v);
                        break;
                    }
                    case Row::Fullscreen: {
                        bool cur = window_->fullscreen();
                        bool now = ui.toggle("Fullscreen", ix, iy, iw, itemH, cur, f);
                        if (now != cur) setFullscreen(now);
                        break;
                    }
                    case Row::Graphics:
                        if (ui.button(qualityLabel(), ix, iy, iw, itemH, f)) pending = i;
                        break;
                    case Row::Display:
                        if (ui.button(displayLabel(ui, std::max(9, (int)std::lround(18 * sc)), iw - 24 * sc), ix, iy, iw, itemH, f)) pending = i;
                        break;
                    case Row::Mode:
                        if (ui.button(std::string("Mode: ") + modeTitle(window_->videoMode()), ix, iy, iw, itemH, f)) pending = i;
                        break;
                    case Row::Resolution:
                        if (ui.button(resolutionLabel(), ix, iy, iw, itemH, f)) pending = i;
                        break;
                    case Row::Back:
                        if (ui.button("Back", ix, iy, iw, itemH, f)) pending = i;
                        break;
                }
            }
        }

        if (pending >= 0) activate(pending);

        if (!status_.empty())
            ui.textCentered(W / 2, py + ph - pad - 14 * sc, status_, fs(15), statusOk_ ? ui.theme.accent : core::Color{1.0f, 0.45f, 0.4f, 1.0f});
    }

    engine::Engine* eng_ = nullptr;
    core::IInput* input_ = nullptr;
    core::RenderEngine* render_ = nullptr;
    core::Window* window_ = nullptr;
    core::UIHandler* ui_ = nullptr;
    core::ISettings* settings_ = nullptr;
    core::ISaveSystem* saves_ = nullptr;
    core::IAudio* audio_ = nullptr;
    core::IQuality* quality_ = nullptr;
    std::vector<Main> main_;
    std::vector<Row> rows_;
    std::vector<core::SlotInfo> slots_;
    Page page_ = Page::Main;
    std::string status_;
    bool statusOk_ = true;
    int focus_ = 0, lastMx_ = -1, lastMy_ = -1;
};

REGISTER_MODULE(PauseMenu);
