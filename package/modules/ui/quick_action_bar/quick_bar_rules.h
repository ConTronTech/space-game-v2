#pragma once
// ui/quick_action_bar - pure rules (no GL, no SDL, no engine): open/close, row x entry selection with wrapping, mouse hover/click selection,
// the idle timeout, per-action cooldowns and availability gating. Unit-tested in package/tests/test_quick_action_bar.cpp.
// See docs/QUICK_ACTION_BAR.md.
//
// The bar's entries are DATA: the module rebuilds the rows from live services every frame (what is installed / in range / fuelled right
// now), so nothing here holds on to an entry. The selection follows the selected entry's id across rebuilds, and cooldowns are keyed by id.
//
// Layout (2026-09-23 redesign): entries are grouped into category ROWS (WEAPONS, SHIP SYSTEMS, ITEMS / PERKS). Up/down moves between rows,
// left/right (and the older prev/next) moves within the current row; both wrap. A row with no entries is skipped as if it did not exist.
//
// Structurally follows the old game's QuickActionBar (include/ui/quickbar/quick_action_bar.h): toggle open, any input while open resets the
// idle timer, confirm fires only when the entry is available and not cooling down, then starts its cooldown.
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

// One category row ("WEAPONS", "SHIP SYSTEMS", "ITEMS / PERKS"). Rows with no entries are skipped by navigation and not drawn.
struct Row {
    std::string name;
    std::vector<Entry> entries;
};

struct Input {
    bool openPressed = false;          // the open action went down this frame
    bool openReleased = false;         // ...and came up (only used in hold mode)
    bool closePressed = false;         // an explicit close (optional binding)
    bool up = false, down = false;     // previous / next ROW (wraps). Default keys W / S, shared with thrust on purpose
    bool left = false, right = false;  // previous / next entry in the current row (wraps). Default keys A / D, shared with strafe
    bool prev = false, next = false;   // same as left / right (the original , / . keys; kept for controllers / wheels)
    bool confirm = false;              // activate the selected entry
    // mouse (only meaningful while the cursor is free; the module fills these from the tiles it drew last frame)
    std::string hoverId;               // the entry under the cursor ("" = none)
    bool mouseMoved = false;           // the cursor moved this frame: only then does hovering take the selection (so a parked cursor never
                                       // fights W/S/A/D)
    bool click = false;                // left button went down this frame: selects AND activates hoverId
};

struct Settings {
    float timeout = 5.0f;              // idle seconds before the bar closes itself (<= 0: never); the old game's TIMEOUT_DURATION
    bool holdToOpen = false;           // true: the bar is open while the open action is held (closes on release). false: press toggles
    bool closeOnUse = false;           // close right after a successful activation
};

struct State {
    bool open = false;
    int row = 0;                       // index into the rows vector (always a non-empty row while there is anything to show)
    int col = 0;                       // index into that row's entries
    std::string selectedId;            // the id (row, col) pointed at, to find it again after the rows are rebuilt
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

inline int entryCount(const std::vector<Row>& rows) {
    int n = 0;
    for (const auto& r : rows) n += (int)r.entries.size();
    return n;
}

// Where an entry id sits: true and (row, col) when found.
inline bool locate(const std::vector<Row>& rows, const std::string& id, int& row, int& col) {
    if (id.empty()) return false;
    for (int r = 0; r < (int)rows.size(); r++)
        for (int c = 0; c < (int)rows[(size_t)r].entries.size(); c++)
            if (rows[(size_t)r].entries[(size_t)c].id == id) { row = r; col = c; return true; }
    return false;
}

// The next non-empty row from `row` in direction dir (+1 / -1), wrapping; `row` itself if it is the only non-empty one.
inline int stepRow(const std::vector<Row>& rows, int row, int dir) {
    const int n = (int)rows.size();
    for (int k = 1; k <= n; k++) {
        int r = wrapIndex(row, n, dir * k);
        if (!rows[(size_t)r].entries.empty()) return r;
    }
    return row;
}

// The selected entry, or nullptr when there is nothing (call resync first).
inline const Entry* selectedEntry(const State& s, const std::vector<Row>& rows) {
    if (s.row < 0 || s.row >= (int)rows.size()) return nullptr;
    const auto& es = rows[(size_t)s.row].entries;
    if (s.col < 0 || s.col >= (int)es.size()) return nullptr;
    return &es[(size_t)s.col];
}

// Re-find the selection after the rows were rebuilt: same id if it still exists, else the old (row, col) clamped into range (moving to the
// nearest following non-empty row if that row emptied out).
inline void resync(State& s, const std::vector<Row>& rows) {
    if (entryCount(rows) == 0) { s.row = s.col = 0; s.selectedId.clear(); return; }
    if (locate(rows, s.selectedId, s.row, s.col)) return;
    s.row = std::clamp(s.row, 0, (int)rows.size() - 1);
    if (rows[(size_t)s.row].entries.empty()) s.row = stepRow(rows, s.row, +1);
    s.col = std::clamp(s.col, 0, (int)rows[(size_t)s.row].entries.size() - 1);
    s.selectedId = rows[(size_t)s.row].entries[(size_t)s.col].id;
}

inline void close(State& s) { s.open = false; s.idle = 0; }

namespace detail {
inline void select(State& s, const std::vector<Row>& rows, int row, int col) {
    s.row = row; s.col = col;
    s.selectedId = rows[(size_t)row].entries[(size_t)col].id;
}
inline Result fire(State& s, const Entry& e, const Settings& cfg) {
    if (!usable(s, e)) return {Outcome::Refused, {}};
    if (e.cooldown > 0) s.cooldowns[e.id] = e.cooldown;
    if (cfg.closeOnUse) close(s);
    return {Outcome::Activated, e.id};
}
} // namespace detail

// One frame of input against the current rows. At most one outcome per frame, in priority order:
//   open/close, mouse click (select + activate), row move (up/down), entry move (left/right, prev/next), mouse hover, confirm.
// Keyboard moves beat hover in the same frame; hover only takes the selection on a frame the cursor actually moved.
inline Result step(State& s, const std::vector<Row>& rows, const Input& in, const Settings& cfg) {
    resync(s, rows);
    const int n = entryCount(rows);
    if (!s.open) {
        if (in.openPressed && n > 0) { s.open = true; s.idle = 0; return {Outcome::Opened, {}}; }
        return {};
    }
    if (n == 0) { close(s); return {Outcome::Closed, {}}; }     // everything it offered went away (e.g. the ship module was disabled)
    int hr = -1, hc = -1;
    const bool hovering = locate(rows, in.hoverId, hr, hc);
    bool anyInput = in.openPressed || in.openReleased || in.closePressed || in.up || in.down || in.left || in.right || in.prev || in.next ||
                    in.confirm || (hovering && (in.mouseMoved || in.click));
    if (anyInput) s.idle = 0;
    if (in.closePressed || (cfg.holdToOpen ? in.openReleased : in.openPressed)) { close(s); return {Outcome::Closed, {}}; }

    if (in.click && hovering) {                                 // click: straight to that entry and use it
        detail::select(s, rows, hr, hc);
        return detail::fire(s, rows[(size_t)hr].entries[(size_t)hc], cfg);
    }
    if (in.up != in.down) {                                     // both at once cancel out
        int r = stepRow(rows, s.row, in.down ? +1 : -1);
        if (r != s.row) {
            detail::select(s, rows, r, std::min(s.col, (int)rows[(size_t)r].entries.size() - 1));   // keep the column where it fits
            return {Outcome::Moved, {}};
        }
    }
    const int dx = ((in.right || in.next) ? 1 : 0) - ((in.left || in.prev) ? 1 : 0);   // right + left (or next + prev) cancel out
    if (dx != 0) {
        const int m = (int)rows[(size_t)s.row].entries.size();
        int c = wrapIndex(s.col, m, dx);
        if (c != s.col) { detail::select(s, rows, s.row, c); return {Outcome::Moved, {}}; }
    }
    if (hovering && in.mouseMoved && (hr != s.row || hc != s.col)) {
        detail::select(s, rows, hr, hc);
        return {Outcome::Moved, {}};
    }
    if (in.confirm) {
        const Entry* e = selectedEntry(s, rows);
        if (!e) return {};
        return detail::fire(s, *e, cfg);
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
