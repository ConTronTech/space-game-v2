// core/input_methods/joystick - joysticks, steering wheels, pedals and shifters through SDL's raw joystick API (not the game-controller database:
// wheels and flight sticks are not in it). One device = many axes / buttons / hats. Per-device profiles in config/input/devices/*.json decide what
// each control does; the values are ADDED to the keyboard's and the mouse's through IInput::contribute. Docs: docs/CONTROLLERS.md.
#include <SDL2/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "core/input_handler/input_api.h"
#include "core/input_handler/input_method.h"
#include "core/input_methods/joystick/joystick_api.h"
#include "core/input_methods/joystick/joystick_profile.h"
#include "core/input_methods/joystick/joystick_rules.h"
#include "core/settings/settings_api.h"
#include "core/window/window.h"
#include "engine/engine.h"
#include "engine/log.h"

namespace fs = std::filesystem;

class JoystickMethod : public engine::Module, public core::InputMethod, public core::IControllers {
public:
    const char* name() const override { return "core/input_methods/joystick"; }
    // Initialises BEFORE core/window (priority -1000) so the dev flags can print and exit before any GL window exists (works over SSH).
    // No hard dependency on core/input_handler for the same reason: this module registers with it in its first onFrameBegin, which runs before the
    // input handler's own (it loads the profile there, and the profile can then route "joystick" bindings to us).
    int priority() const override { return -1600; }

    bool init(engine::Engine& eng) override {
        eng_ = &eng;
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");          // work when the window is unfocused (and headless)
        if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0) { LOG_W("joystick", "SDL joystick init failed (%s): no joystick input", SDL_GetError()); return true; }
        sdlOk_ = true;
        loadProfiles();
        for (int i = 0; i < SDL_NumJoysticks(); i++) openDevice(i);
        openFake(eng);
        for (auto& d : devs_) if (d.open) logDevice(d);
        eng.events.subscribe<core::SdlEvent>([this](const core::SdlEvent& ev) {
            if (ev.e.type == SDL_JOYDEVICEADDED) openDevice(ev.e.jdevice.which);
            else if (ev.e.type == SDL_JOYDEVICEREMOVED) closeDevice(ev.e.jdevice.which);
        });
        eng.services.provide<core::IControllers>(this);

        bool list = eng.hasFlag("list-joysticks") || !eng.flagValue("list-joysticks").empty();
        if (list) { printList(eng.flagValue("list-joysticks") == "json"); finish(); }
        if (eng.hasFlag("joystick-monitor")) { monitor(); finish(); }
        if (eng.hasFlag("joystick-calibrate")) { calibrate(); finish(); }
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        eng.services.withdraw<core::IControllers>();
        if (in_) in_->unregisterMethod(this);
        for (auto& d : devs_) if (d.js) SDL_JoystickClose(d.js);
        devs_.clear();
        if (sdlOk_) SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    }

    void onFrameBegin(engine::Engine& eng) override {
        if (registered_) return;
        registered_ = true;
        in_ = eng.services.get<core::IInput>();
        if (in_) in_->registerMethod(this);
        auto* settings = eng.services.get<core::ISettings>();
        if (settings) {
            readSettings(*settings);
            eng.events.subscribe<core::SettingChanged>([this, settings](const core::SettingChanged& e) { if (e.key.rfind("input.joystick", 0) == 0) readSettings(*settings); });
        }
    }

    // ---- core::InputMethod ----
    const char* device() const override { return "joystick"; }
    void clearBindings() override {}
    bool addBinding(const std::string&, const engine::Json&, std::string& err) override {
        err = "joystick bindings live in config/input/devices/*.json (one profile per device), not in the keyboard/mouse profile";
        return false;
    }

    static constexpr int kSettlePolls = 45;

    void poll(core::IInput& in) override {
        if (!enabled_ || !sdlOk_) return;
        SDL_JoystickUpdate();
        for (auto& d : devs_) {
            if (!d.open) continue;
            readState(d);
            contributions_.clear();
            d.mapper.setWaitFirstMove(!d.fake);                                      // real wheels / sticks can report a wrong first value: 0 until the axis moves (joystick_rules.h)
            d.mapper.evaluate(*d.profile, d.state, tuning_, contributions_);
            // SETTLING: a real wheel / stick reports junk for the first moments (axes at an end, buttons "down"): the mapper still sees those frames (it records its
            // baselines and button edges) but nothing reaches the game until kSettlePolls frames have passed (~0.75 s at 60 fps).
            if (!d.fake && d.polls < kSettlePolls) { d.polls++; continue; }
            for (auto& c : contributions_) in.contribute(c.action, c.value);
        }
    }

    // ---- core::IControllers ----
    const std::vector<core::ControllerInfo>& devices() const override { infos_.clear(); for (auto& d : devs_) if (d.open) infos_.push_back(d.info); return infos_; }
    int deviceCount() const override { int n = 0; for (auto& d : devs_) if (d.open) n++; return n; }
    int rawAxis(int dev, int a) const override { auto* d = at(dev); return d ? d->state.axis(a) : 0; }
    bool rawButton(int dev, int b) const override { auto* d = at(dev); return d && d->state.button(b); }
    int rawHat(int dev, int h) const override { auto* d = at(dev); return d ? d->state.hat(h) : 0; }
    const std::string& lastEvent() const override { return lastEvent_; }
    void reloadProfiles() override {                                            // the in-game controller setup saved a profile: use it now
        std::vector<std::string> was;
        for (auto& d : devs_) was.push_back(d.profile ? d.profile->name : std::string());   // devices point into profiles_: remember by name
        profiles_.clear();
        loadProfiles();
        for (size_t k = 0; k < devs_.size(); k++) {
            Dev& d = devs_[k];
            if (!d.open) continue;
            const joystick::Profile* p = nullptr;
            if (d.fake) for (auto& pr : profiles_) if (pr.name == was[k]) { p = &pr; break; }
            if (!p) p = profileFor(d.info.vid, d.info.pid, d.fake ? std::string() : d.info.name);
            d.profile = p; d.info.profile = p->name; d.mapper.reset();
            logDevice(d);
        }
    }

private:
    struct Dev {
        bool open = false, fake = false;
        SDL_Joystick* js = nullptr;
        int instanceId = -1;
        core::ControllerInfo info;
        const joystick::Profile* profile = nullptr;
        joystick::Mapper mapper;
        int polls = 0;                        // frames since the device opened: the first ones carry junk values on some hardware (see kSettlePolls)
        joystick::RawState state;
    };

    const Dev* at(int slot) const { for (auto& d : devs_) if (d.open && d.info.index == slot) return &d; return nullptr; }

    void readSettings(core::ISettings& s) {
        enabled_ = s.get("input.joystick_enabled", true);
        tuning_.deadzoneScale = std::clamp(s.get("input.joystick_deadzone_scale", 1.0f), 0.5f, 2.0f);
        tuning_.sensitivity = std::clamp(s.get("input.joystick_sensitivity", 1.0f), 0.1f, 4.0f);
    }

    // ---- profiles: config/input/devices/*.json ----
    void loadProfiles() {
        std::error_code ec;
        std::vector<fs::path> files;
        for (auto& e : fs::directory_iterator("config/input/devices", ec)) if (e.path().extension() == ".json") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (auto& f : files) {
            std::ifstream in(f);
            std::stringstream ss; ss << in.rdbuf();
            std::string err;
            engine::Json j = engine::Json::parse(ss.str(), &err);
            joystick::Profile p;
            if (!j.isObject() || !joystick::parseProfile(j, p, err)) { LOG_W("joystick", "%s: %s - ignored (a device that would use it gets the generic fallback)", f.string().c_str(), err.c_str()); continue; }
            if (p.name == "unnamed") p.name = f.stem().string();
            profiles_.push_back(p);
        }
        fallback_.name = "(generic fallback: maps nothing)";
    }

    const joystick::Profile* profileFor(int vid, int pid, const std::string& name) const {
        int i = joystick::matchProfile(profiles_, vid, pid, name);
        return i >= 0 ? &profiles_[(size_t)i] : &fallback_;
    }

    // ---- devices ----
    static std::string guidString(SDL_JoystickGUID g) { char b[64]; SDL_JoystickGetGUIDString(g, b, sizeof b); return b; }

    void openDevice(int deviceIndex) {
        if (!sdlOk_ || deviceIndex < 0 || deviceIndex >= SDL_NumJoysticks()) return;
        SDL_Joystick* js = SDL_JoystickOpen(deviceIndex);
        if (!js) { LOG_W("joystick", "cannot open joystick %d: %s", deviceIndex, SDL_GetError()); return; }
        int instance = (int)SDL_JoystickInstanceID(js);
        int slot = table_.add(instance);
        if (slot < 0) { SDL_JoystickClose(js); return; }                       // already open (present at start AND announced by an event)
        if ((size_t)slot >= devs_.size()) devs_.resize((size_t)slot + 1);
        Dev& d = devs_[(size_t)slot];
        d = Dev{};
        d.open = true; d.js = js; d.instanceId = instance;
        const char* nm = SDL_JoystickName(js);
        d.info.index = slot; d.info.name = nm ? nm : "unknown";
        d.info.vid = SDL_JoystickGetVendor(js); d.info.pid = SDL_JoystickGetProduct(js);
        d.info.guid = guidString(SDL_JoystickGetGUID(js));
        d.info.axes = SDL_JoystickNumAxes(js); d.info.buttons = SDL_JoystickNumButtons(js); d.info.hats = SDL_JoystickNumHats(js);
        d.profile = profileFor(d.info.vid, d.info.pid, d.info.name);
        d.info.profile = d.profile->name;
        d.state.axes.assign((size_t)d.info.axes, 0); d.state.buttons.assign((size_t)d.info.buttons, 0); d.state.hats.assign((size_t)d.info.hats, 0);
        if (registered_) logDevice(d);                                          // hot-plug after start: say so (the start-up ones are logged as a block)
    }

    void closeDevice(int instanceId) {
        int slot = table_.remove(instanceId);
        if (slot < 0 || (size_t)slot >= devs_.size()) return;
        Dev& d = devs_[(size_t)slot];
        LOG_I("joystick", "unplugged: %s", d.info.name.c_str());
        if (d.js) SDL_JoystickClose(d.js);
        d = Dev{};
    }

    // --fake-joystick=<profile file stem, profile name or vid:pid> (+ --fake-axis=IDX:VALUE[,..] --fake-button=IDX[,..] --fake-hat=IDX:MASK): a virtual device
    void openFake(engine::Engine& eng) {
        std::string spec = eng.flagValue("fake-joystick");
        if (spec.empty()) return;
        Dev d;
        d.open = true; d.fake = true; d.info.fake = true;
        d.info.name = "Fake joystick (" + spec + ")";
        int vid = -1, pid = -1;
        auto colon = spec.find(':');
        if (colon != std::string::npos) { vid = joystick::parseHex(spec.substr(0, colon)); pid = joystick::parseHex(spec.substr(colon + 1)); }
        const joystick::Profile* p = nullptr;
        if (vid >= 0 && pid >= 0) p = profileFor(vid, pid, "");
        else for (auto& pr : profiles_) if (joystick::lowerCase(pr.name).find(joystick::lowerCase(spec)) != std::string::npos || joystick::lowerCase(pr.match.nameContains).find(joystick::lowerCase(spec)) != std::string::npos) { p = &pr; break; }
        if (!p) { LOG_W("joystick", "--fake-joystick=%s: no such profile; using the generic fallback", spec.c_str()); p = &fallback_; }
        if (p->match.vid >= 0) { d.info.vid = p->match.vid; d.info.pid = p->match.pid; }
        d.info.guid = "fake"; d.info.axes = 8; d.info.buttons = 32; d.info.hats = 1;
        d.profile = p; d.info.profile = p->name;
        d.state.axes.assign(8, 0); d.state.buttons.assign(32, 0); d.state.hats.assign(1, 0);
        auto split = [](const std::string& s) { std::vector<std::string> out; size_t pos = 0; while (pos <= s.size()) { size_t e = s.find(',', pos); out.push_back(s.substr(pos, e == std::string::npos ? std::string::npos : e - pos)); if (e == std::string::npos) break; pos = e + 1; } return out; };
        for (auto& a : split(eng.flagValue("fake-axis"))) {
            auto c = a.find(':');
            if (c == std::string::npos) continue;
            int idx = std::atoi(a.c_str()); float v = std::clamp((float)std::atof(a.c_str() + c + 1), -1.0f, 1.0f);
            if (idx >= 0 && idx < 8) d.state.axes[(size_t)idx] = (int)std::lround(v * (v < 0 ? 32768.0f : 32767.0f));
        }
        for (auto& b : split(eng.flagValue("fake-button"))) { int idx = b.empty() ? -1 : std::atoi(b.c_str()); if (idx >= 0 && idx < 32) d.state.buttons[(size_t)idx] = 1; }
        for (auto& h : split(eng.flagValue("fake-hat"))) { auto c = h.find(':'); if (c != std::string::npos && std::atoi(h.c_str()) == 0) d.state.hats[0] = std::atoi(h.c_str() + c + 1); }
        int slot = table_.add(-1000 - (int)devs_.size());
        if ((size_t)slot >= devs_.size()) devs_.resize((size_t)slot + 1);
        d.info.index = slot;
        devs_[(size_t)slot] = std::move(d);
    }

    void readState(Dev& d) {
        if (d.fake) return;                                                     // a fake device holds the values from the flags
        for (int i = 0; i < d.info.axes; i++) d.state.axes[(size_t)i] = SDL_JoystickGetAxis(d.js, i);
        for (int i = 0; i < d.info.buttons; i++) d.state.buttons[(size_t)i] = SDL_JoystickGetButton(d.js, i) ? 1 : 0;
        for (int i = 0; i < d.info.hats; i++) d.state.hats[(size_t)i] = SDL_JoystickGetHat(d.js, i);
    }

    void logDevice(const Dev& d) {
        LOG_I("joystick", "%s: %s  %04x:%04x  %d axes, %d buttons, %d hats  guid %s  profile: %s%s", d.fake ? "fake device" : "device", d.info.name.c_str(), d.info.vid, d.info.pid,
              d.info.axes, d.info.buttons, d.info.hats, d.info.guid.c_str(), d.info.profile.c_str(), d.profile == &fallback_ ? "  (run --list-joysticks and --joystick-calibrate)" : "");
    }

    // ---- dev flags: no window, exit when done ----
    void finish() { std::fflush(stdout); SDL_Quit(); std::exit(0); }

    void printList(bool json) {
        if (json) {
            engine::Json arr = engine::Json::array();
            for (auto& d : devs_) if (d.open)
                arr.push(engine::Json::object().set("index", d.info.index).set("name", d.info.name).set("vid", (double)d.info.vid).set("pid", (double)d.info.pid).set("guid", d.info.guid)
                             .set("axes", d.info.axes).set("buttons", d.info.buttons).set("hats", d.info.hats).set("profile", d.info.profile).set("fake", d.fake));
            std::fputs(engine::Json::object().set("devices", arr).dump().c_str(), stdout);
            return;
        }
        std::printf("%d joystick device(s), %zu profile(s) in config/input/devices:\n", deviceCount(), profiles_.size());
        for (auto& d : devs_) if (d.open)
            std::printf("  #%d  %-32s  %04x:%04x  %2d axes %2d buttons %d hats  guid %s\n        profile: %s%s\n", d.info.index, d.info.name.c_str(), d.info.vid, d.info.pid, d.info.axes,
                        d.info.buttons, d.info.hats, d.info.guid.c_str(), d.info.profile.c_str(), d.fake ? "   [FAKE]" : "");
        if (deviceCount() == 0) std::printf("  (none: plug a device in, or try --fake-joystick=<profile> for a virtual one)\n");
    }

    // live changes with the game's own mapping: raw axis / button values and the action values they produce
    void monitor() {
        std::printf("joystick monitor: 30 s. Move controls one at a time; lines show the axis or button number, the raw value and what the profile maps it to.\n");
        std::vector<joystick::RawState> last(devs_.size());
        std::vector<std::vector<long>> lastPrint(devs_.size());
        auto t0 = std::chrono::steady_clock::now();
        bool first = true;
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < (eng_->hasFlag("fake-joystick") ? 1.0 : 30.0)) {
            SDL_JoystickUpdate();
            for (size_t k = 0; k < devs_.size(); k++) {
                Dev& d = devs_[k];
                if (!d.open) continue;
                readState(d);
                if (last[k].axes.size() != d.state.axes.size()) { last[k] = joystick::RawState{}; last[k].axes.assign(d.state.axes.size(), 0x7fffffff); last[k].buttons.assign(d.state.buttons.size(), 0); last[k].hats.assign(d.state.hats.size(), 0); }
                for (size_t a = 0; a < d.state.axes.size(); a++) {
                    if (first || last[k].axes[a] == 0x7fffffff || std::abs(d.state.axes[a] - last[k].axes[a]) > 700) {
                        if (first && d.state.axes[a] == 0 && !d.fake) { last[k].axes[a] = d.state.axes[a]; continue; }
                        std::printf("  #%zu axis %zu = %6d  (%+.2f)  %s\n", k, a, d.state.axes[a], joystick::normalizeBipolar(d.state.axes[a], {}), describeAxis(d, (int)a).c_str());
                        last[k].axes[a] = d.state.axes[a];
                    }
                }
                for (size_t b = 0; b < d.state.buttons.size(); b++)
                    if (d.state.buttons[b] != last[k].buttons[b]) { if (first && !d.state.buttons[b]) { last[k].buttons[b] = 0; continue; } std::printf("  #%zu button %zu %s  %s\n", k, b, d.state.buttons[b] ? "DOWN" : "up", describeButton(d, (int)b).c_str()); last[k].buttons[b] = d.state.buttons[b]; }
                for (size_t h = 0; h < d.state.hats.size(); h++)
                    if (d.state.hats[h] != last[k].hats[h]) { std::printf("  #%zu hat %zu = %d\n", k, h, d.state.hats[h]); last[k].hats[h] = d.state.hats[h]; }
                if (first) { contributions_.clear(); d.mapper.evaluate(*d.profile, d.state, tuning_, contributions_); for (auto& c : contributions_) std::printf("      -> action %s = %+.3f\n", c.action.c_str(), c.value); }
            }
            first = false;
            std::fflush(stdout);
            SDL_Delay(16);
        }
        if (deviceCount() == 0) std::printf("(no devices)\n");
    }
    std::string describeAxis(const Dev& d, int a) const {
        for (auto& m : d.profile->axes) if (m.index == a) return m.role == joystick::Role::CombinedPedals ? "-> " + m.above + " / " + m.below : "-> " + m.action;
        return "(not mapped)";
    }
    std::string describeButton(const Dev& d, int b) const {
        for (auto& m : d.profile->buttons) if (m.index == b) return "-> " + m.action;
        for (auto& g : d.profile->groups) for (auto& [idx, v] : g.buttons) if (idx == b) return "-> " + g.action + " = " + std::to_string(v);
        return "(not mapped)";
    }

    // 10 s of the user moving everything: min / max / first value per axis, and a suggested profile snippet in logs/joystick_calibration.json
    void calibrate() {
        std::printf("joystick calibration: 10 s. Move EVERY control to both ends: turn the wheel lock to lock, press each pedal fully, move the stick, twist, the slider...\n");
        std::vector<joystick::CalibrationTracker> trackers(devs_.size());
        auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < (eng_->hasFlag("fake-joystick") ? 0.5 : 10.0)) {
            SDL_JoystickUpdate();
            for (size_t k = 0; k < devs_.size(); k++) if (devs_[k].open) { readState(devs_[k]); trackers[k].update(devs_[k].state.axes); }
            SDL_Delay(8);
        }
        engine::Json devices = engine::Json::array();
        for (size_t k = 0; k < devs_.size(); k++) {
            if (!devs_[k].open) continue;
            engine::Json axes = engine::Json::array();
            std::printf("\ndevice #%zu %s (%04x:%04x)\n", k, devs_[k].info.name.c_str(), devs_[k].info.vid, devs_[k].info.pid);
            const auto& r = trackers[k].ranges();
            for (size_t a = 0; a < r.size(); a++) {
                const char* role = joystick::CalibrationTracker::suggestRole(r[a]);
                std::printf("  axis %zu: min %6d  max %6d  start %6d   %s\n", a, r[a].min, r[a].max, r[a].first, role);
                if (!r[a].moved()) continue;
                axes.push(engine::Json::object().set("index", (int)a).set("role", std::string(role).find("pedal") != std::string::npos ? "throttle" : "stick")
                              .set("rest", std::string(role).find("max") != std::string::npos ? "max" : "min")
                              .set("calibration", engine::Json::object().set("min", r[a].min).set("max", r[a].max).set("centre", r[a].first))
                              .set("note", std::string("suggested: ") + role + "; set the action yourself"));
            }
            devices.push(engine::Json::object().set("name", devs_[k].info.name).set("vid", (double)devs_[k].info.vid).set("pid", (double)devs_[k].info.pid).set("axes", axes));
        }
        std::error_code ec;
        fs::create_directories("logs", ec);
        std::ofstream out("logs/joystick_calibration.json");
        out << engine::Json::object().set("comment", "Suggested axis calibration from --joystick-calibrate. Copy the axes you use into config/input/devices/<vid>_<pid>_<name>.json and add an action to each.").set("devices", devices).dump();
        std::printf("\nwrote logs/joystick_calibration.json\n");
    }

    engine::Engine* eng_ = nullptr;
    core::IInput* in_ = nullptr;
    bool sdlOk_ = false, registered_ = false, enabled_ = true;
    joystick::Tuning tuning_;
    joystick::DeviceTable table_;
    std::vector<Dev> devs_;
    std::vector<joystick::Profile> profiles_;
    joystick::Profile fallback_;
    std::vector<joystick::Contribution> contributions_;
    mutable std::vector<core::ControllerInfo> infos_;
    std::string lastEvent_;
};

REGISTER_MODULE(JoystickMethod);
