// ui/controller_setup - the in-game Controllers screen (joysticks / wheels / pedals / shifters): DEVICES (what is plugged in and which profile it got),
// LIVE (every axis / button / hat of one device, the joystick sliders and a TEST strip of the action values the game really gets) and SETUP (a guided
// wizard that asks for one control at a time and writes config/input/devices/<vid>_<pid>_<slug>.json). Opened from pause menu > Settings > Controllers,
// or at start with --controller-setup[=devices|live|setup|demo]. The game is paused while it is open, so testing the controls never flies the ship.
// Pure logic (wizard, profile generator, layout): controller_setup_rules.h. Docs: docs/CONTROLLERS.md, docs/UI.md.
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include "core/input_handler/input_api.h"
#include "core/input_methods/joystick/joystick_api.h"
#include "core/settings/settings_api.h"
#include "core/ui_handler/ui_handler.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "ui/controller_setup/controller_setup_api.h"
#include "ui/controller_setup/controller_setup_rules.h"
#include "ui/game_menu/menu_rules.h"

namespace fs = std::filesystem;

class ControllerSetup : public engine::Module, public ui::IControllerSetup {
public:
    const char* name() const override { return "ui/controller_setup"; }
    std::vector<std::string> dependencies() const override { return {"core/ui_handler", "core/input_handler"}; }
    std::vector<std::string> optionalDependencies() const override { return {"core/input_methods/joystick", "core/settings", "ui/pause_menu"}; }

    bool init(engine::Engine& eng) override {
        eng_ = &eng;
        ui_ = &eng.services.require<core::UIHandler>();
        input_ = &eng.services.require<core::IInput>();
        outDir_ = eng.flagValue("controller-setup-out", "config/input/devices");
        std::string v = eng.flagValue("controller-setup");
        if (eng.hasFlag("controller-setup") || !v.empty()) {
            startOpen_ = true;
            if (v == "live") view_ = View::Live;
            else if (v == "setup") view_ = View::Setup;
            else if (v == "demo") { view_ = View::Setup; demo_ = true; }
        }
        ui_->addPanel("ui/controller_setup", 1100, [this](core::UIHandler& ui) { if (open_) draw(ui); });   // above the pause menu (1000)
        eng.services.provide<ui::IControllerSetup>(this);
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        if (open_) close();
        ui_->removePanel("ui/controller_setup");
        eng.services.withdraw<ui::IControllerSetup>();
    }

    // Esc closes this screen and must not also reach the pause menu (the same swallow as the game menu: docs/GAME_MENU.md).
    void onFrameBegin(engine::Engine&) override {
        bool close = false;
        if (swallow_.step(open_, input_->value("pause"), close)) input_->contribute("pause", -1.0f);
        if (close) closeRequested_ = true;
    }

    void onUpdate(engine::Engine& eng, float) override {
        if (startOpen_ && eng.frame() >= 1) { startOpen_ = false; open(); }
        if (closeRequested_) { closeRequested_ = false; close(); return; }
        if (!open_) return;
        pads_ = eng.services.get<core::IControllers>();
        pickDevice();
        if (input_->pressed("ui_left")) view_ = (View)(((int)view_ + 2) % 3);
        if (input_->pressed("ui_right")) view_ = (View)(((int)view_ + 1) % 3);
        const Uint8* k = SDL_GetKeyboardState(nullptr);
        bool enter = k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER], bksp = k[SDL_SCANCODE_BACKSPACE], space = k[SDL_SCANCODE_SPACE];
        bool enterP = enter && !enterWas_, bkspP = bksp && !bkspWas_, spaceP = space && !spaceWas_;
        enterWas_ = enter; bkspWas_ = bksp; spaceWas_ = space;
        trackLive(eng);
        if (view_ != View::Setup) return;
        feedWizard();
        if (enterP) { if (wiz_.finished()) save(); else wiz_.confirm(); }
        if (bkspP) wiz_.back();
        if (spaceP) wiz_.skip();
    }

    // ---- ui::IControllerSetup ----
    void open() override {
        if (open_) return;
        open_ = true; openFrame_ = (long)eng_->frame();
        if (!eng_->paused()) { eng_->setPaused(true); ownPause_ = true; }       // like menu.pause_game = true: the wheel under test must not fly the ship
        wiz_.restart(); demoT_ = 0; status_.clear();
        LOG_I("controllers", "controller setup opened");
    }
    void close() override {
        if (!open_) return;
        open_ = false;
        if (ownPause_) { ownPause_ = false; eng_->setPaused(false); }              // opened from the pause menu: back to it (still paused)
        LOG_I("controllers", "controller setup closed");
    }
    bool isOpen() const override { return open_; }

private:
    enum class View { Devices, Live, Setup };

    // ---- device selection + per-device live tracking (sized once per device, no per-frame allocation) ----
    void pickDevice() {
        if (!pads_ || pads_->deviceCount() == 0) { selIdx_ = -1; return; }
        const auto& ds = pads_->devices();
        for (auto& d : ds) if (d.index == selSlot_) { selIdx_ = (int)(&d - ds.data()); return; }
        selIdx_ = 0; selectSlot(ds[0].index);                                   // unplugged (or first time): the first device
    }
    void selectSlot(int slot) {
        selSlot_ = slot;
        std::fill(last_.begin(), last_.end(), 0); std::fill(flash_.begin(), flash_.end(), -1e9); std::fill(presses_.begin(), presses_.end(), 0); std::fill(prevBtn_.begin(), prevBtn_.end(), 0);
        wiz_.restart(); demoT_ = 0;
    }
    const core::ControllerInfo* sel() const { return pads_ && selIdx_ >= 0 && selIdx_ < (int)pads_->devices().size() ? &pads_->devices()[(size_t)selIdx_] : nullptr; }

    void trackLive(engine::Engine& eng) {
        const auto* d = sel();
        if (!d) return;
        if ((int)last_.size() < d->axes) { last_.resize((size_t)d->axes, 0); flash_.resize((size_t)d->axes, -1e9); }
        if ((int)presses_.size() < d->buttons) { presses_.resize((size_t)d->buttons, 0); prevBtn_.resize((size_t)d->buttons, 0); }
        for (int a = 0; a < d->axes; a++) {
            int v = pads_->rawAxis(d->index, a);
            if (std::abs(v - last_[(size_t)a]) > 600) { flash_[(size_t)a] = eng.time(); last_[(size_t)a] = v; }
        }
        for (int b = 0; b < d->buttons; b++) {
            bool down = pads_->rawButton(d->index, b);
            if (down && !prevBtn_[(size_t)b]) presses_[(size_t)b]++;
            prevBtn_[(size_t)b] = down;
        }
    }

    // ---- wizard ----
    void feedWizard() {
        if (wiz_.finished()) return;
        if (demo_) {
            // scripted PXN V10 (controller_setup_rules.h simulatePxn): press, hold, confirm; optional steps without a simulated control are skipped
            if (wiz_.step() != demoStep_) { demoStep_ = wiz_.step(); demoT_ = 0; }
            csetup::simulatePxn(wiz_.current(), demoT_, snap_);
            wiz_.feed(snap_);
            if (wiz_.current().optional && std::string(wiz_.current().key) != "clutch" && demoT_ >= 20) wiz_.skip();
            else if (demoT_ >= 30) wiz_.confirm();
            demoT_++;
            if (wiz_.finished()) save();
            return;
        }
        const auto* d = sel();
        if (!d) return;
        snap_.axes.resize((size_t)d->axes); snap_.buttons.resize((size_t)d->buttons);   // no-ops after the first frame
        for (int a = 0; a < d->axes; a++) snap_.axes[(size_t)a] = pads_->rawAxis(d->index, a);
        for (int b = 0; b < d->buttons; b++) snap_.buttons[(size_t)b] = pads_->rawButton(d->index, b) ? 1 : 0;
        wiz_.feed(snap_);
    }

    void save() {
        std::string name = demo_ ? "PXN V10 (setup demo)" : sel() ? sel()->name : "device";
        int vid = demo_ ? 0x11ff : sel() ? sel()->vid : 0, pid = demo_ ? 0x3245 : sel() ? sel()->pid : 0;
        char date[32]; std::time_t t = std::time(nullptr); std::strftime(date, sizeof date, "%Y-%m-%d %H:%M", std::localtime(&t));
        std::string text = csetup::buildProfile(name, vid, pid, wiz_.steps(), wiz_.results(), date);
        std::error_code ec;
        fs::create_directories(outDir_, ec);
        fs::path file = fs::path(outDir_) / csetup::profileFileName(vid, pid, name);
        // never overwrite silently: the same file, and any other profile for this vid:pid (it would win the match), become .bak
        std::string prefix = csetup::hex4(vid) + "_" + csetup::hex4(pid) + "_";
        std::vector<fs::path> old;
        for (auto& e : fs::directory_iterator(outDir_, ec)) if (e.path().extension() == ".json" && e.path().filename().string().rfind(prefix, 0) == 0) old.push_back(e.path());
        for (auto& p : old) { fs::path bak = p; bak += ".bak"; fs::rename(p, bak, ec); if (!ec) LOG_I("controllers", "kept the old profile as %s", bak.string().c_str()); }
        std::ofstream out(file);
        out << text;
        out.close();
        if (!out) { status_ = "SAVE FAILED: " + file.string(); statusOk_ = false; LOG_W("controllers", "%s", status_.c_str()); return; }
        status_ = "SAVED " + file.filename().string(); statusOk_ = true;
        LOG_I("controllers", "wrote %s", file.string().c_str());
        if (pads_) pads_->reloadProfiles();                                       // takes effect now, no restart
    }

    // ---- drawing ----
    int fs(int px) const { return std::max(9, (int)std::lround(px * L_.s)); }

    void draw(core::UIHandler& ui) {
        if ((long)eng_->frame() == openFrame_) return;                           // the click that opened it must not also click in here
        float W = (float)ui.width(), H = (float)ui.height();
        L_ = csetup::computeLayout(W, H, ui.scale);
        const auto& P = L_.panel;
        ui.rect(0, 0, W, H, 0.0f, 0.02f, 0.05f, 0.5f);
        ui.roundedRect(P.x, P.y, P.w, P.h, 16 * L_.s, {0.02f, 0.03f, 0.06f, 0.8f}, {0.02f, 0.03f, 0.06f, 0.8f});
        ui.glass(P.x, P.y, P.w, P.h, 1.0f, false, 16 * L_.s);
        static const std::vector<std::string> kTabs = {"DEVICES", "LIVE", "SETUP"};
        view_ = (View)ui.tabs(kTabs, (int)view_, L_.tabs.x, L_.tabs.y, L_.tabs.w, L_.tabs.h);
        const auto& B = L_.body;
        switch (view_) {
            case View::Devices: drawDevices(ui, B); break;
            case View::Live: drawLive(ui, B); break;
            case View::Setup: drawSetup(ui, B); break;
        }
        ui.text(L_.footer.x, L_.footer.y, "Esc: close     Left / Right: switch view", fs(13), ui.theme.textDim);
    }

    void drawDevices(core::UIHandler& ui, const csetup::Rect& B) {
        if (!pads_) { ui.text(B.x, B.y, "Joystick input is off (core/input_methods/joystick not loaded)", fs(16), ui.theme.textDim); return; }
        const auto& ds = pads_->devices();
        if (ds.empty()) { ui.text(B.x, B.y, "No joystick, wheel or pedals connected. Plug one in: it appears here.", fs(16), ui.theme.textDim); return; }
        float rh = 44 * L_.s, y = B.y;
        char b[160];
        for (auto& d : ds) {
            if (y + rh > B.y + B.h) break;
            bool generic = d.profile.rfind("(generic", 0) == 0;
            std::snprintf(b, sizeof b, "%04x:%04x   %d axes  %d buttons  %d hats   %s", d.vid, d.pid, d.axes, d.buttons, d.hats, generic ? "(generic: maps nothing)" : d.profile.c_str());
            if (ui.listRow(d.name, b, B.x, y, B.w, rh, d.index == selSlot_)) { selectSlot(d.index); view_ = View::Live; }
            y += rh + 6 * L_.s;
        }
        ui.text(B.x, y + 8 * L_.s, "Click a device to see it live. SETUP writes a profile for the selected device.", fs(13), ui.theme.textDim);
    }

    // one bipolar or pedal bar with its label; raw int16 -> 0..1 fill
    void axisRow(core::UIHandler& ui, float x, float y, float w, int a, int v) {
        bool lit = eng_->time() - flash_[(size_t)a] < 0.4;
        char b[16];
        std::snprintf(b, sizeof b, "AXIS %d", a);
        ui.text(x, y + 3 * L_.s, b, fs(13), lit ? ui.theme.accent : ui.theme.textDim);
        float bx = x + 70 * L_.s, bw = std::max(20.0f, w - 70 * L_.s - 70 * L_.s);
        ui.bar(bx, y + 6 * L_.s, bw, 12 * L_.s, std::clamp((v + 32768) / 65535.0f, 0.0f, 1.0f), lit ? core::Color{0.45f, 0.95f, 1.0f, 1} : core::Color{0.3f, 0.55f, 0.85f, 1});
        ui.rect(bx + bw / 2, y + 3 * L_.s, 1, 18 * L_.s, 1, 1, 1, 0.35f);          // the centre
        std::snprintf(b, sizeof b, "%6d", v);
        ui.text(bx + bw + 8 * L_.s, y + 3 * L_.s, b, fs(13), ui.theme.text);
    }

    void drawLive(core::UIHandler& ui, const csetup::Rect& B) {
        const auto* d = sel();
        if (!d) { ui.text(B.x, B.y, "No device selected.", fs(16), ui.theme.textDim); return; }
        char b[160];
        std::snprintf(b, sizeof b, "%s   %04x:%04x   profile: %s", d->name.c_str(), d->vid, d->pid, d->profile.c_str());
        ui.text(B.x, B.y, b, fs(15), ui.theme.text);
        float top = B.y + 26 * L_.s, stripH = 150 * L_.s;
        float areaH = B.h - 26 * L_.s - stripH;
        // axes: left half
        float colW = B.w * 0.5f - 10 * L_.s;
        int rows = std::max(1, (int)(areaH / L_.axisRowH));
        for (int a = 0; a < d->axes && a < (int)last_.size(); a++) {
            if (a >= rows * 1) break;
            axisRow(ui, B.x, top + a * L_.axisRowH, colW, a, pads_->rawAxis(d->index, a));
        }
        // buttons: right half, boxes lit while down, with the press count
        float gx = B.x + B.w * 0.5f + 10 * L_.s, box = L_.buttonBox, gap = 6 * L_.s;
        int cols = std::max(1, (int)((B.w * 0.5f - 10 * L_.s) / (box + gap)));
        float hatSpace = d->hats > 0 ? 70 * L_.s : 0.0f;
        for (int i = 0; i < d->buttons && i < (int)presses_.size(); i++) {
            float x = gx + (i % cols) * (box + gap), y = top + (i / cols) * (box + gap);
            if (y + box > top + areaH - hatSpace) break;
            bool down = pads_->rawButton(d->index, i);
            ui.glass(x, y, box, box, down ? 1.0f : 0.5f, down, 6 * L_.s);
            if (down) ui.rect(x + 3, y + 3, box - 6, box - 6, 0.3f, 0.9f, 0.5f, 0.75f);
            std::snprintf(b, sizeof b, "%d", i);
            ui.textCentered(x + box / 2, y + 3 * L_.s, b, fs(12), ui.theme.text);
            if (presses_[(size_t)i]) { std::snprintf(b, sizeof b, "x%d", presses_[(size_t)i]); ui.textCentered(x + box / 2, y + box - 15 * L_.s, b, fs(10), ui.theme.accent); }
        }
        // the hat: a small cross
        if (d->hats > 0) {
            int h = pads_->rawHat(d->index, 0);
            float cx = gx + 30 * L_.s, cy = top + areaH - 36 * L_.s, s = 12 * L_.s;
            auto arm = [&](float x, float y, bool on) { ui.rect(x - s / 2, y - s / 2, s, s, on ? 0.3f : 0.3f, on ? 0.9f : 0.36f, on ? 0.5f : 0.45f, on ? 0.95f : 0.6f); };
            arm(cx, cy - s, h & 1); arm(cx + s, cy, h & 2); arm(cx, cy + s, h & 4); arm(cx - s, cy, h & 8);
            ui.text(cx + 2 * s, cy - 8 * L_.s, "HAT 0", fs(12), ui.theme.textDim);
        }
        // TEST strip: what the game actually gets (IInput sums the keyboard, the mouse and this profile)
        float sy = B.y + B.h - stripH + 8 * L_.s;
        ui.text(B.x, sy, "TEST: the action values the game receives", fs(13), ui.theme.textDim);
        static const char* kActions[] = {"yaw", "thrust", "brake", "lift", "roll", "pitch"};
        float cw = (B.w - 5 * 10 * L_.s) / 6;
        for (int i = 0; i < 6; i++) {
            float x = B.x + i * (cw + 10 * L_.s), y = sy + 20 * L_.s;
            float v = input_->value(kActions[i]);
            std::snprintf(b, sizeof b, "%s %+.2f", kActions[i], v);
            ui.text(x, y, b, fs(13), std::fabs(v) > 0.01f ? ui.theme.text : ui.theme.textDim);
            ui.bar(x, y + 20 * L_.s, cw, 10 * L_.s, 0.0f, ui.theme.accent);
            float half = cw / 2, len = std::clamp(v, -1.0f, 1.0f) * half;
            ui.rect(len >= 0 ? x + half : x + half + len, y + 20 * L_.s, std::fabs(len), 10 * L_.s, 0.35f, 0.85f, 1.0f, 0.9f);
            ui.rect(x + half, y + 18 * L_.s, 1, 14 * L_.s, 1, 1, 1, 0.5f);
        }
        // the joystick module's user multipliers (settings, applied live)
        auto* settings = eng_->services.get<core::ISettings>();
        if (settings) {
            float y = sy + 66 * L_.s, w = (B.w - 12 * L_.s) / 2, h = 50 * L_.s;
            float dz = settings->get("input.joystick_deadzone_scale", 1.0f);
            float nd = ui.slider("Dead zone scale", B.x, y, w, h, dz, 0.5f, 2.0f, false, "%.2f");
            if (nd != dz) settings->set("input.joystick_deadzone_scale", nd);
            float se = settings->get("input.joystick_sensitivity", 1.0f);
            float ns = ui.slider("Sensitivity", B.x + w + 12 * L_.s, y, w, h, se, 0.1f, 4.0f, false, "%.2f");
            if (ns != se) settings->set("input.joystick_sensitivity", ns);
        }
    }

    void drawSetup(core::UIHandler& ui, const csetup::Rect& B) {
        if (!demo_ && !sel()) { ui.text(B.x, B.y, "Connect a joystick or wheel first.", fs(16), ui.theme.textDim); return; }
        char b[200];
        const auto& steps = wiz_.steps();
        const auto& res = wiz_.results();
        std::snprintf(b, sizeof b, "SETUP: %s", demo_ ? "PXN V10 (setup demo, simulated)" : sel()->name.c_str());
        ui.text(B.x, B.y, b, fs(15), ui.theme.textDim);
        float bw = 120 * L_.s, bh = 38 * L_.s;
        if (!wiz_.finished()) {
            std::snprintf(b, sizeof b, "STEP %zu OF %zu", wiz_.step() + 1, steps.size());
            ui.text(B.x, B.y + 30 * L_.s, b, fs(14), core::Color{1.0f, 0.86f, 0.35f, 1});
            ui.glass(B.x, B.y + 54 * L_.s, B.w, 64 * L_.s, 1.0f, true, 12 * L_.s);
            ui.textCentered(B.x + B.w / 2, B.y + 72 * L_.s, wiz_.current().prompt, fs(20), ui.theme.text);
            float y = B.y + 132 * L_.s;
            if (wiz_.pending()) {
                ui.textCentered(B.x + B.w / 2, y, csetup::describe(wiz_.current(), wiz_.currentResult()), fs(22), wiz_.ready() ? core::Color{0.4f, 0.95f, 0.5f, 1} : core::Color{1.0f, 0.86f, 0.35f, 1});
                if (!wiz_.ready()) ui.textCentered(B.x + B.w / 2, y + 30 * L_.s, "keep holding it...", fs(14), ui.theme.textDim);
            } else ui.textCentered(B.x + B.w / 2, y, "waiting...", fs(18), ui.theme.textDim);
            float by = y + 60 * L_.s, bx = B.x + B.w / 2 - (bw * 4 + 30 * L_.s) / 2;
            if (wiz_.ready() && ui.button("OK", bx, by, bw, bh, true)) wiz_.confirm();
            if (wiz_.pending() && ui.button("RETRY", bx + (bw + 10 * L_.s), by, bw, bh, false)) wiz_.retry();
            if (ui.button("BACK", bx + 2 * (bw + 10 * L_.s), by, bw, bh, false)) wiz_.back();
            if (wiz_.current().optional && !wiz_.pending() && ui.button("SKIP", bx + 3 * (bw + 10 * L_.s), by, bw, bh, false)) wiz_.skip();
            ui.textCentered(B.x + B.w / 2, by + bh + 10 * L_.s, "Enter: OK     Backspace: retry / back     Space: skip (optional steps)", fs(12), ui.theme.textDim);
        } else {
            ui.text(B.x, B.y + 30 * L_.s, "SUMMARY", fs(18), ui.theme.text);
        }
        // the list of steps: done ones with what they got (the whole list is the summary at the end)
        float ly = B.y + (wiz_.finished() ? 60 : 280) * L_.s, lh = 20 * L_.s;
        int per = std::max(1, (int)((B.y + B.h - (wiz_.finished() ? bh + 16 * L_.s : 0) - ly) / lh));
        float colW = B.w / ((int)(steps.size() + per - 1) / per);
        for (size_t i = 0; i < steps.size(); i++) {
            const auto& r = res[i];
            float x = B.x + (float)(i / (size_t)per) * colW, y = ly + (float)(i % (size_t)per) * lh;
            if (r.done) std::snprintf(b, sizeof b, "%s%s", csetup::describe(steps[i], r).c_str() + 4, *steps[i].action ? "" : " (no action)");   // "AXIS 2 (rests low, pressed +)"
            else std::snprintf(b, sizeof b, "%s", r.skipped ? "skipped" : i == wiz_.step() ? "<" : "-");
            ui.text(x, y, steps[i].key, fs(13), r.done ? ui.theme.text : ui.theme.textDim);
            ui.text(x + 110 * L_.s, y, b, fs(13), r.done ? ui.theme.accent : ui.theme.textDim);
        }
        if (wiz_.finished()) {
            float by = B.y + B.h - bh;
            if (ui.button("SAVE", B.x, by, bw, bh, true)) save();
            if (ui.button("BACK", B.x + bw + 10 * L_.s, by, bw, bh, false)) wiz_.back();
            if (ui.button("RESTART", B.x + 2 * (bw + 10 * L_.s), by, bw, bh, false)) { wiz_.restart(); status_.clear(); }
            if (!status_.empty()) ui.text(B.x + 3 * (bw + 10 * L_.s), by + 10 * L_.s, status_, fs(13), statusOk_ ? core::Color{0.4f, 0.95f, 0.5f, 1} : core::Color{1.0f, 0.45f, 0.4f, 1});
        }
    }

    engine::Engine* eng_ = nullptr;
    core::UIHandler* ui_ = nullptr;
    core::IInput* input_ = nullptr;
    core::IControllers* pads_ = nullptr;
    ui::PauseSwallow swallow_;
    bool open_ = false, ownPause_ = false, closeRequested_ = false, startOpen_ = false, demo_ = false;
    bool enterWas_ = false, bkspWas_ = false, spaceWas_ = false, statusOk_ = true;
    long openFrame_ = -1;
    View view_ = View::Devices;
    int selSlot_ = -1, selIdx_ = -1, demoT_ = 0;
    size_t demoStep_ = 0;
    std::string outDir_, status_;
    csetup::Wizard wiz_;
    csetup::Snapshot snap_;
    csetup::Layout L_;
    std::vector<int> last_, presses_;
    std::vector<double> flash_;
    std::vector<unsigned char> prevBtn_;
};

REGISTER_MODULE(ControllerSetup);
