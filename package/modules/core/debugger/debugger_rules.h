#pragma once
// core/debugger - the testable logic (no SDL/GL): the watch registry, grouping by prefix, the event-log ring, draw-hook toggles,
// and the dump file format (write + parse). The module (debugger.cpp) is a thin shell around these.
#include <cstdio>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace dbg {

// "gravity.dominant" -> "gravity"; a name without a '.' goes to "misc".
inline std::string groupOf(const std::string& name) {
    size_t p = name.find('.');
    return (p == std::string::npos || p == 0) ? std::string("misc") : name.substr(0, p);
}
// "gravity.dominant" -> "dominant" (the whole name when there is no group)
inline std::string leafOf(const std::string& name) {
    size_t p = name.find('.');
    return (p == std::string::npos || p == 0) ? name : name.substr(p + 1);
}

// Named live values. A getter that throws shows "<error: ...>" instead of taking the game down.
class WatchRegistry {
public:
    using Getter = std::function<std::string()>;
    // true = a new name; false = it replaced an existing one
    bool add(const std::string& name, Getter g) {
        auto it = entries_.find(name);
        if (it != entries_.end()) { it->second.getter = std::move(g); return false; }
        entries_[name] = Entry{std::move(g), "-"};
        return true;
    }
    bool remove(const std::string& name) { return entries_.erase(name) > 0; }
    bool has(const std::string& name) const { return entries_.count(name) > 0; }
    size_t size() const { return entries_.size(); }
    // Calls every getter once and caches the text (the panel polls this at debugger.watch_hz while it is open).
    void poll() {
        for (auto& [name, e] : entries_) {
            if (!e.getter) { e.value = "-"; continue; }
            try { e.value = e.getter(); }
            catch (const std::exception& ex) { e.value = std::string("<error: ") + ex.what() + ">"; }
            catch (...) { e.value = "<error>"; }
        }
    }
    std::string value(const std::string& name) const {
        auto it = entries_.find(name);
        return it == entries_.end() ? std::string() : it->second.value;
    }
    // (name, cached value), sorted by name
    std::vector<std::pair<std::string, std::string>> values() const {
        std::vector<std::pair<std::string, std::string>> out;
        out.reserve(entries_.size());
        for (auto& [n, e] : entries_) out.emplace_back(n, e.value);
        return out;
    }
private:
    struct Entry { Getter getter; std::string value; };
    std::map<std::string, Entry> entries_;
};

struct Group { std::string name; std::vector<std::pair<std::string, std::string>> rows; };   // rows: (leaf, value)
// Groups sorted by name, rows keep the input order (already sorted by full name).
inline std::vector<Group> groupByPrefix(const std::vector<std::pair<std::string, std::string>>& values) {
    std::map<std::string, Group> m;
    for (auto& [n, v] : values) {
        std::string g = groupOf(n);
        Group& gr = m[g];
        gr.name = g;
        gr.rows.emplace_back(leafOf(n), v);
    }
    std::vector<Group> out;
    for (auto& [k, g] : m) out.push_back(std::move(g));
    return out;
}

// Fixed-capacity ring of log lines, oldest first.
class LogRing {
public:
    explicit LogRing(size_t capacity = 200) : cap_(capacity < 1 ? 1 : capacity) {}
    void push(std::string line) {
        if (lines_.size() < cap_) lines_.push_back(std::move(line));
        else { lines_[head_] = std::move(line); head_ = (head_ + 1) % cap_; }
        total_++;
    }
    size_t size() const { return lines_.size(); }
    size_t capacity() const { return cap_; }
    unsigned long total() const { return total_; }            // lines ever pushed (how many were dropped = total - size)
    const std::string& at(size_t i) const { return lines_[(head_ + i) % lines_.size()]; }   // 0 = oldest kept
    std::vector<std::string> all() const {
        std::vector<std::string> out;
        out.reserve(lines_.size());
        for (size_t i = 0; i < lines_.size(); i++) out.push_back(at(i));
        return out;
    }
    void clear() { lines_.clear(); head_ = 0; }
private:
    size_t cap_;
    size_t head_ = 0;
    unsigned long total_ = 0;
    std::vector<std::string> lines_;
};

// Draw hooks: name -> fn + on/off. Toggles survive re-registration (a module reloading its hook keeps the player's choice).
template <class Fn>
class DrawHooks {
public:
    bool add(const std::string& name, Fn fn) {
        auto it = hooks_.find(name);
        if (it != hooks_.end()) { it->second.fn = std::move(fn); it->second.registered = true; return false; }
        bool on = pendingOn_.count(name) > 0;
        hooks_[name] = Hook{std::move(fn), on, true};
        return true;
    }
    bool remove(const std::string& name) {
        auto it = hooks_.find(name);
        if (it == hooks_.end()) return false;
        if (it->second.on) pendingOn_[name] = 1; else pendingOn_.erase(name);
        hooks_.erase(it);
        return true;
    }
    bool has(const std::string& name) const { return hooks_.count(name) > 0; }
    bool enabled(const std::string& name) const { auto it = hooks_.find(name); return it != hooks_.end() && it->second.on; }
    // Switching on a name that is not registered yet (e.g. --debug-draw on the command line before the module inits) is remembered.
    void setEnabled(const std::string& name, bool on) {
        auto it = hooks_.find(name);
        if (it != hooks_.end()) it->second.on = on;
        if (on) pendingOn_[name] = 1; else pendingOn_.erase(name);
    }
    std::vector<std::string> names() const { std::vector<std::string> v; for (auto& [n, h] : hooks_) v.push_back(n); return v; }
    int enabledCount() const { int c = 0; for (auto& [n, h] : hooks_) c += h.on ? 1 : 0; return c; }
    template <class F> void forEachEnabled(F&& f) const { for (auto& [n, h] : hooks_) if (h.on && h.fn) f(n, h.fn); }
private:
    struct Hook { Fn fn; bool on = false; bool registered = true; };
    std::map<std::string, Hook> hooks_;
    std::map<std::string, int> pendingOn_;
};

// ---------- the dump file (logs/debug_state.txt) ----------
// Format (line based, self-explanatory, parseable back by parseDump):
//   # comment / explanation lines
//   [header]           key: value lines (build, uptime, ...)
//   [watches]          then per group "== group ==" and "  leaf = value"
//   [draw hooks]       "  name: on|off"
//   [event log]        "  line" (oldest first)
struct DumpData {
    std::vector<std::pair<std::string, std::string>> header;       // (key, value)
    std::vector<std::pair<std::string, std::string>> watches;      // (full name, value)
    std::vector<std::pair<std::string, bool>> hooks;               // (name, on)
    std::vector<std::string> log;
};

inline std::string oneLine(std::string s) {
    for (char& c : s) if (c == '\n' || c == '\r') c = ' ';
    return s;
}

inline std::string formatDump(const DumpData& d) {
    std::ostringstream o;
    o << "# SPACE GAME V2 - DEBUG STATE SNAPSHOT (written by F8, core/debugger; see docs/DEBUGGER.md)\n";
    o << "# What this is: every live value modules registered with the debugger at the moment F8 was pressed,\n";
    o << "# which debug draws were on, and the last game events (oldest first). Paste this file back with a description\n";
    o << "# of the weird thing you saw. For a PERFORMANCE problem also press F5 and send logs/profile_live.txt.\n";
    o << "# Watch names are 'group.value' (the group is the module that registered it). Times in the log are game seconds.\n\n";
    o << "[header]\n";
    for (auto& [k, v] : d.header) o << k << ": " << oneLine(v) << "\n";
    o << "\n[watches]\n";
    std::string cur;
    bool first = true;
    for (auto& [n, v] : d.watches) {
        std::string g = groupOf(n);
        if (first || g != cur) { o << "== " << g << " ==\n"; cur = g; first = false; }
        o << "  " << leafOf(n) << " = " << oneLine(v) << "\n";
    }
    o << "\n[draw hooks]\n";
    for (auto& [n, on] : d.hooks) o << "  " << n << ": " << (on ? "on" : "off") << "\n";
    o << "\n[event log]\n";
    for (auto& l : d.log) o << "  " << oneLine(l) << "\n";
    return o.str();
}

inline DumpData parseDump(const std::string& text) {
    DumpData d;
    std::istringstream in(text);
    std::string line, section, group;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') { section = line; group.clear(); continue; }
        if (section == "[header]") {
            size_t p = line.find(": ");
            if (p != std::string::npos) d.header.emplace_back(line.substr(0, p), line.substr(p + 2));
        } else if (section == "[watches]") {
            if (line.rfind("== ", 0) == 0 && line.size() >= 6) { group = line.substr(3, line.size() - 6); continue; }
            if (line.rfind("  ", 0) != 0) continue;
            std::string body = line.substr(2);
            size_t p = body.find(" = ");
            if (p == std::string::npos) continue;
            std::string leaf = body.substr(0, p), v = body.substr(p + 3);
            d.watches.emplace_back(group == "misc" && leaf.find('.') == std::string::npos ? leaf : group + "." + leaf, v);
        } else if (section == "[draw hooks]") {
            if (line.rfind("  ", 0) != 0) continue;
            std::string body = line.substr(2);
            size_t p = body.rfind(": ");
            if (p != std::string::npos) d.hooks.emplace_back(body.substr(0, p), body.substr(p + 2) == "on");
        } else if (section == "[event log]") {
            if (line.rfind("  ", 0) == 0) d.log.push_back(line.substr(2));
        }
    }
    return d;
}

inline std::string fmtTime(double t) { char b[32]; std::snprintf(b, sizeof b, "%9.2f", t); return b; }
inline std::string fmtVec(double x, double y, double z) { char b[96]; std::snprintf(b, sizeof b, "(%.1f, %.1f, %.1f)", x, y, z); return b; }

} // namespace dbg
