#pragma once
// ui/quick_action_bar - pure rules (no GL, no SDL, no engine): open/close, selection wrapping, the idle timeout, per-action cooldowns and
// availability gating. Unit-tested in package/tests/test_quick_action_bar.cpp. See docs/QUICK_ACTION_BAR.md.
//
// The bar's entries are DATA: the module rebuilds the list from live services every frame (what is installed / in range / fuelled right now),
// so nothing here holds on to an entry. The selection follows the selected entry's id across rebuilds, and cooldowns are keyed by id.
//
// Structurally follows the old game's QuickActionBar (include/ui/quickbar/quick_action_bar.h): toggle open, any input while open resets the
// idle timer, left/right wraps, confirm fires only when the entry is available and not cooling down, then starts its cooldown.
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace quickbar {

enum class Kind { Instant, Toggle };   // Instant: fires once. Toggle: flips something on/off; `on` is its live state (shown ON/OFF).

struct Entry {
    std::string id;                    // stable key ("weapon_1", "warp", "orbit_lock", ...): selection + cooldowns follow it
    std::string label;                 // "BLASTER"
    std::string detail;                // small second line ("x3", "Planet 2", "not aligned"); may be empty
    Kind kind = Kind::Instant;
    bool available = true;             // false: drawn greyed out, confirm is refused
    bool on = false;                   // Toggle: current state. Instant: "this is the current one" (e.g. the selected weapon)
    float cooldown = 0;                // seconds this entry is locked after firing (0 = none)
};

struct Input {
    bool openPressed = false;          // the open action went down this frame
    bool openReleased = false;         // ...and came up (only used in hold mode)
    bool closePressed = false;         // an explicit close (optional binding)
    bool prev = false, next = false;   // move the selection (wraps)
    bool confirm = false;              // activate the selected entry
};

struct Settings {
    float timeout = 5.0f;              // idle seconds before the bar closes itself (<= 0: never); the old game's TIMEOUT_DURATION
    bool holdToOpen = false;           // true: the bar is open while the open action is held (closes on release). false: press toggles
    bool closeOnUse = false;           // close right after a successful activation
};

struct State {
    bool open = false;
    int selected = 0;
    std::string selectedId;            // the id `selected` pointed at, to find it again after the list is rebuilt
    float idle = 0;                    // seconds since the last input while open
    std::map<std::string, float> cooldowns;   // id -> seconds left (> 0 only)
};

enum class Outcome { None, Opened, Closed, Moved, Activated, Refused };

struct Result {
    Outcome outcome = Outcome::None;
    std::string activated;             // the entry id, for Outcome::Activated
};

inline float cooldownLeft(const State& s, const std::string& id) {
    auto it = s.cooldowns.find(id);
    return it == s.cooldowns.end() ? 0.0f : it->second;
}

// 0..1: how much of the entry's cooldown is still left (drawn as the fill-down overlay). 0 when it is ready.
inline float cooldownFraction(const State& s, const Entry& e) {
    if (e.cooldown <= 0) return 0.0f;
    return std::clamp(cooldownLeft(s, e.id) / e.cooldown, 0.0f, 1.0f);
}

// Can the entry fire right now?
inline bool usable(const State& s, const Entry& e) { return e.available && cooldownLeft(s, e.id) <= 0; }

// (i + dir) wrapped into 0..n-1; 0 for an empty list.
inline int wrapIndex(int i, int n, int dir) {
    if (n <= 0) return 0;
    return ((i + dir) % n + n) % n;
}

// Re-find the selection after the entry list was rebuilt: same id if it still exists, else the old index clamped into range.
inline void resync(State& s, const std::vector<Entry>& entries) {
    const int n = (int)entries.size();
    if (n == 0) { s.selected = 0; s.selectedId.clear(); return; }
    for (int i = 0; i < n; i++)
        if (entries[(size_t)i].id == s.selectedId) { s.selected = i; return; }
    s.selected = std::clamp(s.selected, 0, n - 1);
    s.selectedId = entries[(size_t)s.selected].id;
}

inline void close(State& s) { s.open = false; s.idle = 0; }

// One frame of input against the current entries. At most one outcome per frame, in priority order: open/close, move, confirm.
inline Result step(State& s, const std::vector<Entry>& entries, const Input& in, const Settings& cfg) {
    resync(s, entries);
    const int n = (int)entries.size();
    if (!s.open) {
        if (in.openPressed && n > 0) { s.open = true; s.idle = 0; return {Outcome::Opened, {}}; }
        return {};
    }
    if (n == 0) { close(s); return {Outcome::Closed, {}}; }     // everything it offered went away (e.g. the ship module was disabled)
    bool anyInput = in.openPressed || in.openReleased || in.closePressed || in.prev || in.next || in.confirm;
    if (anyInput) s.idle = 0;
    if (in.closePressed || (cfg.holdToOpen ? in.openReleased : in.openPressed)) { close(s); return {Outcome::Closed, {}}; }
    if (in.prev != in.next) {                                   // both at once cancel out
        s.selected = wrapIndex(s.selected, n, in.next ? +1 : -1);
        s.selectedId = entries[(size_t)s.selected].id;
        return {Outcome::Moved, {}};
    }
    if (in.confirm) {
        const Entry& e = entries[(size_t)s.selected];
        if (!usable(s, e)) return {Outcome::Refused, {}};
        if (e.cooldown > 0) s.cooldowns[e.id] = e.cooldown;
        if (cfg.closeOnUse) close(s);
        return {Outcome::Activated, e.id};
    }
    return {};
}

// Advance time: cooldowns always run down (also while the bar is closed), the idle timer only while it is open.
// Returns true when the idle timeout closed the bar this call.
inline bool tick(State& s, float dt, const Settings& cfg) {
    for (auto it = s.cooldowns.begin(); it != s.cooldowns.end();) {
        it->second -= dt;
        if (it->second <= 0) it = s.cooldowns.erase(it); else ++it;
    }
    if (!s.open) return false;
    s.idle += dt;
    if (cfg.timeout > 0 && s.idle >= cfg.timeout) { close(s); return true; }
    return false;
}

// 0..1 of the idle timeout left (for a thin "closing soon" bar). 1 when the timeout is off.
inline float timeoutFraction(const State& s, const Settings& cfg) {
    if (cfg.timeout <= 0) return 1.0f;
    return std::clamp(1.0f - s.idle / cfg.timeout, 0.0f, 1.0f);
}

// The input action a weapon slot maps to (weapon_1..weapon_3 in config/input/default.json); "" past the bound slots.
inline std::string weaponAction(int index) {
    if (index < 0 || index > 2) return {};
    return "weapon_" + std::to_string(index + 1);
}

} // namespace quickbar
