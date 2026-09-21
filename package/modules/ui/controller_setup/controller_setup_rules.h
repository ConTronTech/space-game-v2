#pragma once
// Pure logic of the in-game controller setup (ui/controller_setup): the guided wizard state machine, the profile generator and the layout math.
// NO SDL / GL / engine types (the generated profile is plain JSON text). Unit-tested in package/tests/test_controller_setup.cpp. Docs: docs/CONTROLLERS.md.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace csetup {

// ---------------------------------------------------------------- one reading of a device (raw int16 axes, 0/1 buttons)
struct Snapshot {
    std::vector<int> axes;
    std::vector<unsigned char> buttons;
    int axis(int i) const { return i >= 0 && i < (int)axes.size() ? axes[(size_t)i] : 0; }
    bool button(int i) const { return i >= 0 && i < (int)buttons.size() && buttons[(size_t)i] != 0; }
};

// ---------------------------------------------------------------- the steps
enum class Kind { Axis, Button };
enum class AxisUse { Steering, Throttle, Brake, Clutch, None };

struct StepDef {
    const char* key;          // "steering", "fire", ...
    const char* prompt;       // the big line on screen
    Kind kind;
    bool optional;
    const char* action;       // the game action it maps to (config/input/default.json); "" = none exists yet (the choice is only noted in the profile's header)
    AxisUse use = AxisUse::None;
};

// Every action below exists in config/input/default.json except weapon_prev (there is no "previous weapon" action in the game yet).
inline const std::vector<StepDef>& defaultSteps() {
    static const std::vector<StepDef> s = {
        {"steering", "TURN THE WHEEL FULLY RIGHT (OR PUSH THE STICK RIGHT) AND HOLD", Kind::Axis, false, "yaw", AxisUse::Steering},
        {"throttle", "PRESS THE THROTTLE PEDAL FULLY DOWN AND HOLD", Kind::Axis, false, "thrust", AxisUse::Throttle},
        {"brake", "PRESS THE BRAKE PEDAL FULLY DOWN AND HOLD", Kind::Axis, false, "brake", AxisUse::Brake},
        {"clutch", "PRESS THE CLUTCH PEDAL FULLY DOWN AND HOLD (OPTIONAL: LIFT DOWN)", Kind::Axis, true, "lift", AxisUse::Clutch},
        {"fire", "PRESS THE BUTTON YOU WANT FOR FIRE", Kind::Button, false, "fire"},
        {"dock", "PRESS THE BUTTON YOU WANT FOR DOCK", Kind::Button, false, "dock"},
        {"warp", "PRESS THE BUTTON YOU WANT FOR WARP", Kind::Button, false, "toggle_warp"},
        {"orbit_lock", "PRESS THE BUTTON YOU WANT FOR ORBIT LOCK", Kind::Button, false, "toggle_orbit_lock"},
        {"menu", "PRESS THE BUTTON YOU WANT FOR THE GAME MENU", Kind::Button, false, "toggle_menu"},
        {"camera", "PRESS THE BUTTON YOU WANT FOR THE CAMERA VIEW", Kind::Button, false, "camera_next"},
        {"weapon_next", "PRESS THE BUTTON YOU WANT FOR NEXT WEAPON", Kind::Button, false, "weapon_next"},
        {"weapon_prev", "PRESS THE BUTTON YOU WANT FOR PREVIOUS WEAPON (OPTIONAL)", Kind::Button, true, ""},
        {"lock_target", "PRESS THE BUTTON YOU WANT FOR LOCK TARGET (OPTIONAL)", Kind::Button, true, "lock_target"},
        {"lock_clear", "PRESS THE BUTTON YOU WANT FOR CLEAR LOCK (OPTIONAL)", Kind::Button, true, "lock_clear"},
    };
    return s;
}

// What the wizard found for one step.
struct StepResult {
    bool done = false, skipped = false;
    int index = -1;           // axis or button number
    int baseline = 0;         // axis: the reading when the step began (after the settle frames)
    int held = 0;             // axis: the latest reading while pending / at confirmation (the pressed position)
    int minSeen = 0, maxSeen = 0;
    int dir() const { return held >= baseline ? 1 : -1; }
};

// Resting end of a pedal from where it is HELD pressed (robust to a wrong first value: the held position is always a real reading).
// Held near the top -> it rests at the bottom ("min"); held near the bottom -> "max".
inline bool restsLow(const StepResult& r) { return r.held >= 0; }

// "GOT AXIS 6 (rests low, pressed +)" / "GOT BUTTON 3"
inline std::string describe(const StepDef& d, const StepResult& r) {
    char b[96];
    if (d.kind == Kind::Button) std::snprintf(b, sizeof b, "GOT BUTTON %d", r.index);
    else if (d.use == AxisUse::Steering) std::snprintf(b, sizeof b, "GOT AXIS %d (centred, right = %c)", r.index, r.held >= 0 ? '+' : '-');
    else std::snprintf(b, sizeof b, "GOT AXIS %d (rests %s, pressed %c)", r.index, restsLow(r) ? "low" : "high", restsLow(r) ? '+' : '-');
    return b;
}

// ---------------------------------------------------------------- the wizard
// Detection is by CHANGE from a baseline taken at the start of each step, never by absolute value: some wheels report a wrong first axis value
// (the PXN V10 read +32767 on the steering axis until its first motion). Extra guards:
//  * the baseline follows the device for kSettleFrames after the step begins (late first reports land there);
//  * a one-sample jump from an END of the range to near the CENTRE is the device correcting a wrong first value: re-baseline, not a candidate;
//  * an axis candidate is only confirmable once it is held away from the centre (|held| >= kHoldMin): "keep holding" otherwise;
//  * an axis or button already used by an earlier step is ignored; a button already down when the step began must be released first.
class Wizard {
public:
    static constexpr int kAxisThreshold = 20000;   // the same threshold joytest --guided used
    static constexpr int kSettleFrames = 6;
    static constexpr int kCentreBand = 3000;
    static constexpr int kEndBand = 26000;
    static constexpr int kHoldMin = 16000;

    explicit Wizard(std::vector<StepDef> steps = defaultSteps()) : steps_(std::move(steps)), res_(steps_.size()) {}

    void restart() { res_.assign(steps_.size(), StepResult{}); step_ = 0; pending_ = false; frames_ = 0; }

    // Feed one reading per frame.
    void feed(const Snapshot& s) {
        if (finished()) return;
        if (frames_ == 0 || base_.axes.size() != s.axes.size() || base_.buttons.size() != s.buttons.size()) { base_ = s; prev_ = s; }
        if (frames_ < kSettleFrames) { base_.axes = s.axes; frames_++; prev_ = s; syncButtons(s); return; }
        frames_++;
        const StepDef& d = steps_[step_];
        StepResult& r = res_[step_];
        if (pending_) {
            if (d.kind == Kind::Axis) {
                int v = s.axis(r.index);
                r.held = v; r.minSeen = std::min(r.minSeen, v); r.maxSeen = std::max(r.maxSeen, v);
            }
        } else if (d.kind == Kind::Axis) {
            for (int a = 0; a < (int)s.axes.size(); a++) {
                int v = s.axes[(size_t)a], b = base_.axes[(size_t)a], p = prev_.axis(a);
                if (std::abs(v - b) < kAxisThreshold) continue;
                if (std::abs(p) >= kEndBand && std::abs(v) <= kCentreBand && std::abs(p - b) < kAxisThreshold / 4) { base_.axes[(size_t)a] = v; continue; }   // a wrong first value corrected
                if (used(Kind::Axis, a)) continue;
                pending_ = true;
                r = StepResult{}; r.index = a; r.baseline = b; r.held = v; r.minSeen = std::min(b, v); r.maxSeen = std::max(b, v);
                break;
            }
        } else {
            for (int i = 0; i < (int)s.buttons.size(); i++) {
                bool down = s.buttons[(size_t)i] != 0;
                if (!down) { base_.buttons[(size_t)i] = 0; continue; }               // released: a later press counts
                if (base_.buttons[(size_t)i] || used(Kind::Button, i)) continue;     // held since the step began, or taken
                pending_ = true;
                r = StepResult{}; r.index = i;
                break;
            }
        }
        prev_ = s;
    }

    bool pending() const { return pending_; }
    // A pending axis must be held away from the centre before it can be confirmed (a steering wheel only at rest says nothing about its direction).
    bool ready() const {
        if (!pending_) return false;
        const StepDef& d = steps_[step_];
        if (d.kind == Kind::Button) return true;
        return std::abs(res_[step_].held) >= kHoldMin;
    }
    bool confirm() {
        if (!ready()) return false;
        res_[step_].done = true; pending_ = false;
        advance();
        return true;
    }
    void retry() { if (pending_) { pending_ = false; res_[step_] = StepResult{}; frames_ = 0; } }
    // BACK: drops the pending candidate first; otherwise re-asks the previous step.
    void back() {
        if (pending_) { retry(); return; }
        if (step_ == 0) return;
        step_--;
        res_[step_] = StepResult{};
        frames_ = 0;
    }
    bool skip() {
        if (finished() || pending_ || !steps_[step_].optional) return false;
        res_[step_] = StepResult{}; res_[step_].skipped = true;
        advance();
        return true;
    }
    bool finished() const { return step_ >= steps_.size(); }
    size_t step() const { return step_; }
    const std::vector<StepDef>& steps() const { return steps_; }
    const std::vector<StepResult>& results() const { return res_; }
    const StepDef& current() const { return steps_[std::min(step_, steps_.size() - 1)]; }
    const StepResult& currentResult() const { return res_[std::min(step_, res_.size() - 1)]; }

private:
    void advance() { step_++; frames_ = 0; }
    void syncButtons(const Snapshot& s) { for (size_t i = 0; i < s.buttons.size() && i < base_.buttons.size(); i++) base_.buttons[i] = s.buttons[i]; }
    bool used(Kind k, int idx) const {
        for (size_t i = 0; i < steps_.size(); i++) if (i != step_ && res_[i].done && steps_[i].kind == k && res_[i].index == idx) return true;
        return false;
    }
    std::vector<StepDef> steps_;
    std::vector<StepResult> res_;
    size_t step_ = 0;
    bool pending_ = false;
    int frames_ = 0;
    Snapshot base_, prev_;
};

// ---------------------------------------------------------------- the profile file
inline std::string hex4(int v) { char b[8]; std::snprintf(b, sizeof b, "%04x", v & 0xffff); return b; }

// "PXN-V10 Wheel!" -> "pxn_v10_wheel" (lower-case letters and digits, runs of anything else -> one '_', at most 32 characters, "device" when empty)
inline std::string slug(const std::string& name) {
    std::string out;
    for (char c : name) {
        unsigned char u = (unsigned char)c;
        if (std::isalnum(u) && u < 128) out += (char)std::tolower(u);
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.size() > 32) { out.resize(32); while (!out.empty() && out.back() == '_') out.pop_back(); }
    return out.empty() ? "device" : out;
}
inline std::string profileFileName(int vid, int pid, const std::string& name) { return hex4(vid) + "_" + hex4(pid) + "_" + slug(name) + ".json"; }

inline std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((unsigned char)c < 0x20) o += ' ';
        else o += c;
    }
    return o;
}

// Defaults written into generated profiles.
constexpr float kStickDeadzone = 0.03f, kStickCurve = 1.5f, kPedalDeadzone = 0.02f;

// Builds the profile JSON (the same format as the shipped config/input/devices/11ff_3245_pxn_v10.json, with `//` header comments).
// Sign convention: SDL reports a wheel turned right as + on most devices, and the game's `yaw` + = turn LEFT, so a steering axis that reads + to the
// right gets "invert": true (wheel right -> yaw - -> the ship turns right), one that reads - to the right does not.
// Pedals: "rest" is the end the pedal rests at (from where it was held pressed). Clutch -> `lift` with scale -1 (pressed = lift down).
inline std::string buildProfile(const std::string& deviceName, int vid, int pid, const std::vector<StepDef>& steps, const std::vector<StepResult>& res, const std::string& date) {
    std::string h, axes, buttons;
    h += "// " + deviceName + " (USB " + hex4(vid) + ":" + hex4(pid) + ")\n";
    h += "// GENERATED " + date + " by the in-game controller setup (pause menu > Settings > Controllers > SETUP). Format: docs/CONTROLLERS.md.\n";
    char b[256];
    for (size_t i = 0; i < steps.size() && i < res.size(); i++) {
        const StepDef& d = steps[i];
        const StepResult& r = res[i];
        if (!r.done) { if (r.skipped) h += std::string("// ") + d.key + ": skipped\n"; continue; }
        if (d.kind == Kind::Button) {
            if (!*d.action) { std::snprintf(b, sizeof b, "// %s: button %d chosen, but the game has no action for it yet (not mapped)\n", d.key, r.index); h += b; continue; }
            std::snprintf(b, sizeof b, "%s    { \"index\": %d, \"action\": \"%s\" }", buttons.empty() ? "" : ",\n", r.index, d.action);
            buttons += b;
            continue;
        }
        std::string line;
        if (d.use == AxisUse::Steering) {
            bool invert = r.held >= 0;
            std::snprintf(b, sizeof b, "    { \"index\": %d, \"role\": \"steering\", \"action\": \"%s\", \"invert\": %s, \"deadzone\": %.2f, \"curve\": %.1f }",
                          r.index, d.action, invert ? "true" : "false", kStickDeadzone, kStickCurve);
            line = b;
            std::snprintf(b, sizeof b, "// %s: axis %d, right = %c%s\n", d.key, r.index, invert ? '+' : '-', invert ? " -> invert (yaw + = turn left)" : "");
            h += b;
        } else {
            const char* role = d.use == AxisUse::Throttle ? "throttle" : d.use == AxisUse::Brake ? "brake" : "clutch";
            bool low = restsLow(r);
            std::string cal;
            // pressed short of the end (a pedal that never reaches the stop): calibrate so a full press still reads 1
            int pressedEnd = low ? r.maxSeen : r.minSeen;
            if (low && pressedEnd < 30000) { std::snprintf(b, sizeof b, ", \"calibration\": { \"min\": -32768, \"max\": %d }", pressedEnd); cal = b; }
            if (!low && pressedEnd > -30000) { std::snprintf(b, sizeof b, ", \"calibration\": { \"min\": %d, \"max\": 32767 }", pressedEnd); cal = b; }
            std::snprintf(b, sizeof b, "    { \"index\": %d, \"role\": \"%s\", \"action\": \"%s\", \"rest\": \"%s\", \"deadzone\": %.2f%s%s }",
                          r.index, role, d.action, low ? "min" : "max", kPedalDeadzone, d.use == AxisUse::Clutch ? ", \"scale\": -1" : "", cal.c_str());
            line = b;
            std::snprintf(b, sizeof b, "// %s: axis %d, rests %s (%d), pressed %d\n", d.key, r.index, low ? "low" : "high", r.baseline, r.held);
            h += b;
        }
        axes += (axes.empty() ? "" : ",\n") + line;
    }
    std::string j = h;
    j += "{\n";
    j += "  \"name\": \"" + jsonEscape(deviceName) + "\",\n";
    j += "  \"status\": \"GENERATED " + jsonEscape(date) + " by the in-game setup\",\n";
    j += "  \"match\": { \"vid\": \"" + hex4(vid) + "\", \"pid\": \"" + hex4(pid) + "\" },\n";
    j += "  \"axes\": [\n" + axes + (axes.empty() ? "" : "\n") + "  ],\n";
    j += "  \"buttons\": [\n" + buttons + (buttons.empty() ? "" : "\n") + "  ]\n";
    j += "}\n";
    return j;
}

// ---------------------------------------------------------------- a simulated PXN V10 (docs/DEVICES.md): steering axis 0 (reports a WRONG first
// value +32767 until it moves), throttle 2, brake 5, clutch 6 (pedals rest at -32768), buttons 0 fire, 13 dock, 3 warp, 9 orbit lock, 12 menu,
// 8 camera, 1 next weapon. Used by the tests and by the --controller-setup=demo run. `step` = the wizard step being asked, `t` = frames into it.
inline void simulatePxn(const StepDef& step, int t, Snapshot& s) {
    s.axes.assign(8, 0); s.buttons.assign(16, 0);
    for (int a : {2, 5, 6}) s.axes[(size_t)a] = -32768;
    s.axes[0] = 32767;                                                          // the wrong first value
    std::string k = step.key;
    if (k != "steering") s.axes[0] = 0;                                         // later steps: the wheel has moved once, so it reports the truth
    else if (t >= 10) s.axes[0] = 0;                                            // the first real report: back to the centre (not a candidate)
    int press = t >= 14 ? std::min(32767, -32768 + (t - 13) * 16384) : -32768; // ramps down over 4 frames
    if (k == "steering" && t >= 14) s.axes[0] = std::min(32767, (t - 13) * 8000);
    if (k == "throttle") s.axes[2] = press;
    if (k == "brake") s.axes[5] = press;
    if (k == "clutch") s.axes[6] = press;
    static const struct { const char* key; int button; } kButtons[] = {{"fire", 0}, {"dock", 13}, {"warp", 3}, {"orbit_lock", 9}, {"menu", 12}, {"camera", 8}, {"weapon_next", 1}};
    for (auto& kb : kButtons) if (k == kb.key && t >= 14) s.buttons[(size_t)kb.button] = 1;
}

// ---------------------------------------------------------------- layout (pixels, top-left origin)
struct Rect { float x = 0, y = 0, w = 0, h = 0; bool inside(const Rect& o) const { return x >= o.x - 0.5f && y >= o.y - 0.5f && x + w <= o.x + o.w + 0.5f && y + h <= o.y + o.h + 0.5f; } };

struct Layout {
    float s = 1;              // ui scale
    Rect panel, tabs, body, footer;
    int axisCols = 1, buttonCols = 8;
    float axisRowH = 0, buttonBox = 0;
};

// The panel is ~80% of the window (at least 24 px margin), everything inside scales with `scale` (core::uiLayoutScale).
inline Layout computeLayout(float W, float H, float scale) {
    Layout L; L.s = scale;
    float margin = std::max(12.0f, 0.1f * std::min(W, H));
    L.panel.w = std::min(W * 0.8f, W - margin); L.panel.h = std::min(H * 0.84f, H - margin);
    L.panel.w = std::min(L.panel.w, 1500.0f * scale); L.panel.h = std::min(L.panel.h, 900.0f * scale);
    L.panel.x = (W - L.panel.w) / 2; L.panel.y = (H - L.panel.h) / 2;
    float pad = 16 * scale;
    L.tabs = {L.panel.x + pad, L.panel.y + pad, L.panel.w - 2 * pad, 34 * scale};
    L.footer = {L.panel.x + pad, L.panel.y + L.panel.h - pad - 20 * scale, L.panel.w - 2 * pad, 20 * scale};
    float top = L.tabs.y + L.tabs.h + 12 * scale;
    L.body = {L.panel.x + pad, top, L.panel.w - 2 * pad, L.footer.y - 8 * scale - top};
    L.axisCols = L.body.w > 700 * scale ? 2 : 1;
    L.axisRowH = 26 * scale;
    L.buttonBox = 34 * scale;
    L.buttonCols = std::max(4, (int)((L.body.w * 0.5f) / (L.buttonBox + 6 * scale)));
    return L;
}

} // namespace csetup
