#pragma once
// Pure joystick / wheel / pedal / shifter logic: NO SDL types, no engine types. Unit-tested in package/tests/test_joystick.cpp with simulated devices.
// A device exports many axes, buttons and hats; a Profile says which raw control drives which game action, and the Mapper turns one raw snapshot
// into action contributions (which core::IInput sums with the keyboard's and the mouse's, so the actions are the same names the keyboard profile uses).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace joystick {

// ---------------------------------------------------------------- axes
struct Calibration { int min = -32768, max = 32767, centre = 0; };          // raw int16 values as SDL reports them

// Raw int16 -> -1..1 around the calibrated centre: the two halves are scaled separately, so an off-centre stick still reaches -1 and +1 at its stops.
inline float normalizeBipolar(int raw, const Calibration& c) {
    float v;
    if (raw >= c.centre) { float span = (float)(c.max - c.centre); v = span > 0.0f ? (float)(raw - c.centre) / span : 0.0f; }
    else { float span = (float)(c.centre - c.min); v = span > 0.0f ? (float)(raw - c.centre) / span : 0.0f; }
    return std::clamp(v, -1.0f, 1.0f);
}

// Dead zone with rescaling: |v| <= dz gives 0 and the output rises from 0 right after it, so the value is continuous at the edge and still reaches 1.
inline float applyDeadzone(float v, float dz) {
    dz = std::clamp(dz, 0.0f, 0.99f);
    float a = std::fabs(v);
    if (a <= dz) return 0.0f;
    return std::copysign((a - dz) / (1.0f - dz), v);
}

// Response curve: sign * |v|^exponent (1 = linear, > 1 = gentler near the centre, < 1 = more sensitive).
inline float applyCurve(float v, float exponent) {
    if (exponent <= 0.0f || exponent == 1.0f) return v;
    return std::copysign(std::pow(std::fabs(v), exponent), v);
}

// Which end of the raw range a pedal rests at. Many pedal axes rest at -1 (or +1) and go to the other end when pressed.
enum class Rest { Min, Max };

// Pedal: 0 unpressed .. 1 fully pressed, from the calibrated min/max and the resting end.
inline float normalizePedal(int raw, const Calibration& c, Rest rest) {
    float span = (float)(c.max - c.min);
    if (span <= 0.0f) return 0.0f;
    float v = rest == Rest::Min ? (float)(raw - c.min) / span : (float)(c.max - raw) / span;
    return std::clamp(v, 0.0f, 1.0f);
}

enum class Role { Stick, Steering, Throttle, Brake, Clutch, CombinedPedals };

struct AxisMap {
    int index = 0;
    Role role = Role::Stick;
    std::string action;                  // stick / steering / throttle / brake / clutch: the action driven by this axis
    std::string above, below;            // combined_pedals: the actions above and below the centre (throttle above, brake below by default)
    float scale = 1.0f;                  // multiplies the result (negative = the same as invert)
    bool invert = false;
    float deadzone = 0.0f;               // 0..1 of the calibrated travel
    float curve = 1.0f;
    Calibration cal;
    Rest rest = Rest::Min;               // pedals
};

// The value one axis contributes (before the user's sensitivity): bipolar for stick/steering, 0..1 for pedals.
inline float axisValue(const AxisMap& a, int raw, float deadzoneScale) {
    float dz = a.deadzone * deadzoneScale;
    float v;
    switch (a.role) {
        case Role::Throttle: case Role::Brake: case Role::Clutch:
            v = applyCurve(applyDeadzone(normalizePedal(raw, a.cal, a.rest), dz), a.curve);
            break;
        default:
            v = applyCurve(applyDeadzone(normalizeBipolar(raw, a.cal), dz), a.curve);
            break;
    }
    if (a.invert) v = -v;
    return v * a.scale;
}

// ---------------------------------------------------------------- buttons, hats, groups
enum class ButtonMode { Hold, Toggle };      // Hold: active while pressed (the same as a key: `pressed()` sees the first frame). Toggle: each press flips it.

struct ButtonMap { int index = 0; std::string action; float scale = 1.0f; ButtonMode mode = ButtonMode::Hold; };
struct HatMap { int index = 0; std::string up, down, left, right; };

// Several buttons that select one discrete value of one action (a stick shifter's gears -> throttle_limit steps, a reverse gear -> a flag).
// The highest-numbered pressed button wins; when none is pressed the default applies (if there is one).
struct GroupMap {
    std::string action;
    std::vector<std::pair<int, float>> buttons;   // button index -> value
    bool hasDefault = false;
    float defaultValue = 0.0f;
};

// Hat bits (the same values SDL uses).
constexpr int kHatUp = 1, kHatRight = 2, kHatDown = 4, kHatLeft = 8;

// ---------------------------------------------------------------- profiles
struct Match { int vid = -1, pid = -1; std::string nameContains; };

struct Profile {
    std::string name;
    std::string status;                            // e.g. "UNVERIFIED: run --joystick-calibrate"
    Match match;
    std::vector<AxisMap> axes;
    std::vector<ButtonMap> buttons;
    std::vector<HatMap> hats;
    std::vector<GroupMap> groups;
    bool empty() const { return axes.empty() && buttons.empty() && hats.empty() && groups.empty(); }
};

inline std::string lowerCase(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// The profile for a device: an exact USB vendor+product match first, then a name substring, else -1 (the caller uses the generic fallback).
inline int matchProfile(const std::vector<Profile>& profiles, int vid, int pid, const std::string& deviceName) {
    for (size_t i = 0; i < profiles.size(); i++) {
        const Match& m = profiles[i].match;
        if (m.vid >= 0 && m.pid >= 0 && m.vid == vid && m.pid == pid) return (int)i;
    }
    std::string n = lowerCase(deviceName);
    for (size_t i = 0; i < profiles.size(); i++) {
        const Match& m = profiles[i].match;
        if (!m.nameContains.empty() && n.find(lowerCase(m.nameContains)) != std::string::npos) return (int)i;
    }
    return -1;
}

// ---------------------------------------------------------------- one snapshot of a device
struct RawState {
    std::vector<int> axes;                         // int16 range, as SDL reports
    std::vector<uint8_t> buttons;                  // 0 / 1
    std::vector<int> hats;                         // bitmask of kHat*
    int axis(int i) const { return i >= 0 && i < (int)axes.size() ? axes[(size_t)i] : 0; }
    bool button(int i) const { return i >= 0 && i < (int)buttons.size() && buttons[(size_t)i] != 0; }
    int hat(int i) const { return i >= 0 && i < (int)hats.size() ? hats[(size_t)i] : 0; }
};

struct Tuning { float deadzoneScale = 1.0f; float sensitivity = 1.0f; };   // the user's multipliers (input.joystick_deadzone_scale 0.5..2, input.joystick_sensitivity)

struct Contribution { std::string action; float value = 0; };

// Turns snapshots into action contributions. Keeps the toggle states and the previous button states (per device, so make one per device).
class Mapper {
public:
    void reset() { toggles_.clear(); prev_.clear(); }

    void evaluate(const Profile& p, const RawState& s, const Tuning& t, std::vector<Contribution>& out) {
        float dzs = std::clamp(t.deadzoneScale, 0.5f, 2.0f), sens = std::max(0.0f, t.sensitivity);
        for (auto& a : p.axes) {
            int raw = s.axis(a.index);
            if (a.role == Role::CombinedPedals) {
                // one axis, two pedals: above the centre = the "above" action (throttle), below = the "below" action (brake); each 0..1
                float n = applyCurve(applyDeadzone(normalizeBipolar(raw, a.cal), a.deadzone * dzs), a.curve);
                if (a.invert) n = -n;
                if (!a.above.empty() && n > 0.0f) out.push_back({a.above, n * a.scale * sens});
                if (!a.below.empty() && n < 0.0f) out.push_back({a.below, -n * a.scale * sens});
                continue;
            }
            if (a.action.empty()) continue;
            float v = axisValue(a, raw, dzs);
            bool pedal = a.role == Role::Throttle || a.role == Role::Brake || a.role == Role::Clutch;
            out.push_back({a.action, pedal ? v : v * sens});                    // sensitivity is for stick / steering feel, not for pedals
        }
        size_t maxButton = 0;
        for (auto& b : p.buttons) maxButton = std::max(maxButton, (size_t)b.index + 1);
        if (toggles_.size() < maxButton) toggles_.resize(maxButton, 0);
        if (prev_.size() < maxButton) prev_.resize(maxButton, 0);
        for (auto& b : p.buttons) {
            bool down = s.button(b.index);
            if (b.mode == ButtonMode::Toggle) {
                if (down && !prev_[(size_t)b.index]) toggles_[(size_t)b.index] ^= 1;      // a fresh press flips it
                if (toggles_[(size_t)b.index]) out.push_back({b.action, b.scale});
            } else if (down) out.push_back({b.action, b.scale});
            prev_[(size_t)b.index] = down ? 1 : 0;
        }
        for (auto& h : p.hats) {
            int m = s.hat(h.index);
            if ((m & kHatUp) && !h.up.empty()) out.push_back({h.up, 1.0f});
            if ((m & kHatDown) && !h.down.empty()) out.push_back({h.down, 1.0f});
            if ((m & kHatLeft) && !h.left.empty()) out.push_back({h.left, 1.0f});
            if ((m & kHatRight) && !h.right.empty()) out.push_back({h.right, 1.0f});
        }
        for (auto& g : p.groups) {
            bool any = false; int best = -1; float value = 0;
            for (auto& [idx, v] : g.buttons) if (s.button(idx) && idx > best) { best = idx; value = v; any = true; }
            if (any) out.push_back({g.action, value});
            else if (g.hasDefault) out.push_back({g.action, g.defaultValue});
        }
    }

private:
    std::vector<uint8_t> toggles_, prev_;
};

// ---------------------------------------------------------------- hot-plug
// Which SDL instance ids are open and in which slot. Adding an id twice is ignored (SDL reports the devices present at start AND we enumerate them);
// removing an unknown id is ignored; freed slots are reused lowest first, so a re-plugged wheel gets its old slot back.
class DeviceTable {
public:
    int add(int instanceId) {
        int s = slotOf(instanceId);
        if (s >= 0) return -1;                                   // already known
        for (size_t i = 0; i < ids_.size(); i++) if (ids_[i] == kFree) { ids_[i] = instanceId; return (int)i; }
        ids_.push_back(instanceId);
        return (int)ids_.size() - 1;
    }
    int remove(int instanceId) {
        int s = slotOf(instanceId);
        if (s >= 0) ids_[(size_t)s] = kFree;
        return s;                                                // -1: it was not open
    }
    int slotOf(int instanceId) const { for (size_t i = 0; i < ids_.size(); i++) if (ids_[i] == instanceId) return (int)i; return -1; }
    int count() const { int n = 0; for (int id : ids_) if (id != kFree) n++; return n; }
    int slots() const { return (int)ids_.size(); }
private:
    static constexpr int kFree = std::numeric_limits<int>::min();
    std::vector<int> ids_;
};

// ---------------------------------------------------------------- calibration helper (--joystick-calibrate)
struct AxisRange { bool seen = false; int first = 0, min = 0, max = 0; bool moved(int threshold = 2000) const { return seen && (max - min) > threshold; } };

class CalibrationTracker {
public:
    void update(const std::vector<int>& axes) {
        if (ranges_.size() < axes.size()) ranges_.resize(axes.size());
        for (size_t i = 0; i < axes.size(); i++) {
            AxisRange& r = ranges_[i];
            if (!r.seen) { r.seen = true; r.first = r.min = r.max = axes[i]; }
            else { r.min = std::min(r.min, axes[i]); r.max = std::max(r.max, axes[i]); }
        }
    }
    const std::vector<AxisRange>& ranges() const { return ranges_; }
    // The role a range suggests: a pedal rests at one end (first sample near min or max) and only travels one way; a stick / wheel rests near the middle.
    static const char* suggestRole(const AxisRange& r) {
        if (!r.moved()) return "unused (did not move)";
        int span = r.max - r.min;
        if (r.first <= r.min + span / 10) return "pedal resting at min";
        if (r.first >= r.max - span / 10) return "pedal resting at max";
        return "stick / steering (centred)";
    }
private:
    std::vector<AxisRange> ranges_;
};

} // namespace joystick
