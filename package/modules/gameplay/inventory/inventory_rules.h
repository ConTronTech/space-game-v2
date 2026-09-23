#pragma once
// Pure cargo-hold logic: no GL, no SDL, no engine types. Unit-tested in package/tests/test_inventory.cpp.
//   * ONE unified grid of slots (default 8 x 12 = 96): ores and crafted items share it, no separate pools
//   * the slot COUNT never changes with the cargo upgrade level; the level multiplies each id's STACK CAP instead
//     (base stack = data/ores.json "cargo_cap" for ores, data/items.json "stack_cap" (or 10 / volume) for items)
//   * add auto-stacks: same-id slots first (up to the cap, in slot order), then new empty slots; nothing left = refused
//   * remove is atomic across every same-id slot (taken from the LAST slots first, so full stacks stay together)
//   * slot moves (swap / merge) and single-slot discards for the CARGO grid UI
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>
#include "gameplay/inventory/inventory_api.h"

namespace gameplay {

// ---- levels: data/cargo.json "stack_mult" (older files: resource_mult, then capacity_mult) ----
struct CargoLevel { float stackMult = 1.0f; };
inline std::vector<CargoLevel> defaultCargoLevels() { return {{1}, {2}, {4}, {6}}; }
inline int clampLevel(int level, int count) { return std::clamp(level, 0, std::max(0, count - 1)); }

// ---- the ores and their base stack caps ----
struct ResourceDef {
    std::string id;
    int cap = 20;                 // units per stack at level 0 (data/ores.json "cargo_cap")
    bool counted = true;          // listed by countedResources() (ores of data/ores.json); the filler "rock" is not
};

constexpr int kDefaultResourceCap = 20;      // an ore without "cargo_cap"
constexpr int kRockCap = 100;                // the worthless filler
constexpr int kDefaultItemStack = 10;        // an item (or unknown id) without "stack_cap": 10 per stack at level 0 (divided by its volume, if any)
constexpr int kDefaultGridCols = 8, kDefaultGridRows = 12;

// An item's level-0 stack from items.json: explicit stack_cap wins; else kDefaultItemStack / volume (volume <= 0 -> the default), at least 1.
inline int itemBaseStack(double stackCap, double volume) {
    if (stackCap > 0) return std::max(1, (int)stackCap);
    if (volume > 0) return std::max(1, (int)std::floor(kDefaultItemStack / volume + 1e-6));
    return kDefaultItemStack;
}

// The built-in table (data missing): the same numbers as data/ores.json.
inline std::vector<ResourceDef> defaultResources() {
    return {{"iron", 100, true}, {"copper", 80, true}, {"titanium", 60, true}, {"gold", 50, true}, {"cobalt", 50, true},
            {"uranium", 10, true}, {"platinum", 10, true}, {"crystal", 10, true}};
}

class Cargo {
public:
    Cargo() { setGrid(kDefaultGridCols, kDefaultGridRows); }

    // ---- configuration ----
    void setResources(const std::vector<ResourceDef>& r) {
        resources_ = r;
        bool hasRock = false;
        for (auto& d : resources_) if (d.id == "rock") hasRock = true;
        if (!hasRock) resources_.push_back({"rock", kRockCap, false});       // the filler always stacks, but is not listed as an ore
    }
    // Grid size. Resizing keeps what fits: slots past the new end are dropped (only done at init, before anything is loaded).
    void setGrid(int cols, int rows) {
        cols_ = std::max(1, cols); rows_ = std::max(1, rows);
        slots_.resize((size_t)(cols_ * rows_));
    }
    void setStackMult(float m) { mult_ = std::max(0.0f, m); }
    float stackMult() const { return mult_; }
    void setItemStack(const std::string& id, int base) { itemBase_[id] = std::max(0, base); }

    // ---- grid ----
    int columns() const { return cols_; }
    int rows() const { return rows_; }
    int slotCount() const { return (int)slots_.size(); }
    const std::vector<Stack>& slots() const { return slots_; }            // an empty slot has an empty id and amount 0
    int usedSlots() const { int n = 0; for (auto& s : slots_) if (!s.id.empty()) n++; return n; }
    int freeSlots() const { return slotCount() - usedSlots(); }

    // ---- stack caps ----
    bool isResource(const std::string& id) const { return def(id) != nullptr; }
    int baseStack(const std::string& id) const {
        if (const ResourceDef* d = def(id)) return d->cap;
        auto it = itemBase_.find(id);
        return it == itemBase_.end() ? kDefaultItemStack : it->second;
    }
    int stackCapAt(const std::string& id, float mult) const {
        int b = baseStack(id);
        return b <= 0 ? 0 : std::max(1, (int)std::floor((double)b * std::max(0.0f, mult) + 1e-6));   // a positive base never scales below 1
    }
    int stackCap(const std::string& id) const { return mult_ <= 0 ? 0 : stackCapAt(id, mult_); }

    // ---- contents ----
    int count(const std::string& id) const { long long n = 0; for (auto& s : slots_) if (s.id == id) n += s.amount; return (int)std::min<long long>(n, 2000000000LL); }
    // How many more units of `id` would fit right now: room in its partial slots + every empty slot at a full stack.
    int room(const std::string& id) const { return roomIn(slots_, id); }
    // One entry per id, summed across its slots, in the order the ids first appear in the grid (the old stacks() view).
    std::vector<Stack> totals() const {
        std::vector<Stack> out;
        for (auto& s : slots_) {
            if (s.id.empty()) continue;
            auto it = std::find_if(out.begin(), out.end(), [&](const Stack& o) { return o.id == s.id; });
            if (it == out.end()) out.push_back(s); else it->amount += s.amount;
        }
        return out;
    }
    // Units held of the listed ores (rock excluded) plus every item: for logs / an overview.
    long long totalUnits() const { long long n = 0; for (auto& s : slots_) if (!s.id.empty() && s.id != "rock") n += s.amount; return n; }
    void countedResources(std::vector<std::string>& out) const { out.clear(); for (auto& d : resources_) if (d.counted) out.push_back(d.id); }

    // Adds up to `amount`: tops up same-id slots first (slot order), then fills empty slots (slot order). Returns how much fitted.
    int add(const std::string& id, int amount) { return addTo(slots_, id, amount); }

    // All or nothing across every slot of the id; taken from the last slots first. An emptied slot becomes empty.
    bool remove(const std::string& id, int amount) {
        if (amount <= 0 || id.empty() || count(id) < amount) return false;
        takeFrom(slots_, id, amount);
        return true;
    }

    // The CARGO grid: discard `amount` from ONE slot (amount <= 0 or >= what it holds = the whole slot). Returns what was removed (0 for a bad / empty slot).
    int discardSlot(int slot, int amount, std::string* idOut = nullptr) {
        if (slot < 0 || slot >= slotCount() || slots_[(size_t)slot].id.empty()) return 0;
        Stack& s = slots_[(size_t)slot];
        int n = (amount <= 0 || amount >= s.amount) ? s.amount : amount;
        if (idOut) *idOut = s.id;
        s.amount -= n;
        if (s.amount <= 0) s = Stack{};
        return n;
    }

    // Drag one slot onto another: onto an empty slot = move; onto the same id = merge up to the cap (the rest stays behind); onto another id = swap.
    bool moveSlot(int from, int to) {
        if (from < 0 || to < 0 || from >= slotCount() || to >= slotCount() || from == to) return false;
        Stack& a = slots_[(size_t)from];
        Stack& b = slots_[(size_t)to];
        if (a.id.empty()) return false;
        if (b.id == a.id) {
            int moved = std::min(a.amount, std::max(0, stackCap(a.id) - b.amount));
            if (moved <= 0) std::swap(a, b);                                  // target already full: just swap them (harmless, same id)
            else { b.amount += moved; a.amount -= moved; if (a.amount <= 0) a = Stack{}; }
            return true;
        }
        std::swap(a, b);
        return true;
    }

    // Room for `id` AFTER taking the `removed` stacks out (crafting: do the ingredients free a slot for the result?). Nothing changes.
    int roomAfter(const std::vector<Stack>& removed, const std::string& id) const {
        std::vector<Stack> copy = slots_;
        for (auto& r : removed) if (!r.id.empty() && r.amount > 0) takeFrom(copy, r.id, r.amount);
        return roomIn(copy, id);
    }

    void clear() { for (auto& s : slots_) s = Stack{}; }

    // Replaces the contents with auto-stacked stacks (an old save, or a --give). Bad entries (empty id, amount <= 0) are dropped;
    // anything that does not fit is CLIPPED (returned in `clipped` so the caller can log it). Never crashes on bad data.
    void assign(const std::vector<Stack>& in, std::vector<Stack>* clipped = nullptr) {
        clear();
        if (clipped) clipped->clear();
        for (auto& s : in) {
            if (s.id.empty() || s.amount <= 0) continue;
            int got = add(s.id, s.amount);
            if (got < s.amount && clipped) clipped->push_back({s.id, s.amount - got});
        }
    }

    // Replaces the contents with a saved slot layout (version 3 saves). Each entry goes back into its own slot when that index is valid and free
    // (and up to the stack cap); whatever could not be placed is auto-stacked afterwards, and anything still left over is clipped.
    struct PlacedStack { int slot = -1; Stack stack; };
    void assignSlots(const std::vector<PlacedStack>& in, std::vector<Stack>* clipped = nullptr) {
        clear();
        if (clipped) clipped->clear();
        std::vector<Stack> rest;
        for (auto& p : in) {
            if (p.stack.id.empty() || p.stack.amount <= 0) continue;
            int cap = stackCap(p.stack.id);
            if (p.slot >= 0 && p.slot < slotCount() && slots_[(size_t)p.slot].id.empty() && cap > 0) {
                int keep = std::min(p.stack.amount, cap);
                slots_[(size_t)p.slot] = {p.stack.id, keep};
                if (p.stack.amount > keep) rest.push_back({p.stack.id, p.stack.amount - keep});
            } else rest.push_back(p.stack);
        }
        for (auto& s : rest) {
            int got = add(s.id, s.amount);
            if (got < s.amount && clipped) clipped->push_back({s.id, s.amount - got});
        }
    }

private:
    const ResourceDef* def(const std::string& id) const { for (auto& d : resources_) if (d.id == id) return &d; return nullptr; }

    int roomIn(const std::vector<Stack>& v, const std::string& id) const {
        if (id.empty()) return 0;
        int cap = stackCap(id);
        if (cap <= 0) return 0;
        long long r = 0;
        for (auto& s : v) {
            if (s.id.empty()) r += cap;
            else if (s.id == id) r += std::max(0, cap - s.amount);            // an over-full slot (a lowered level) simply has no room
        }
        return (int)std::min<long long>(r, 2000000000LL);
    }

    int addTo(std::vector<Stack>& v, const std::string& id, int amount) const {
        if (amount <= 0 || id.empty()) return 0;
        int cap = stackCap(id);
        if (cap <= 0) return 0;
        int left = amount;
        for (auto& s : v) {                                                   // 1) top up the stacks we already have
            if (left <= 0) break;
            if (s.id != id || s.amount >= cap) continue;
            int put = std::min(left, cap - s.amount);
            s.amount += put; left -= put;
        }
        for (auto& s : v) {                                                   // 2) spill into empty slots
            if (left <= 0) break;
            if (!s.id.empty()) continue;
            int put = std::min(left, cap);
            s = {id, put}; left -= put;
        }
        return amount - left;
    }

    static void takeFrom(std::vector<Stack>& v, const std::string& id, int amount) {
        for (size_t k = v.size(); k-- > 0 && amount > 0;) {
            if (v[k].id != id) continue;
            int t = std::min(amount, v[k].amount);
            v[k].amount -= t; amount -= t;
            if (v[k].amount <= 0) v[k] = Stack{};
        }
    }

    std::vector<ResourceDef> resources_;
    std::unordered_map<std::string, int> itemBase_;
    std::vector<Stack> slots_;
    int cols_ = kDefaultGridCols, rows_ = kDefaultGridRows;
    float mult_ = 1.0f;
};

// "iron:50,copper:20" -> stacks (dev flag --give). Bad pieces are skipped.
inline std::vector<Stack> parseGive(const std::string& text) {
    std::vector<Stack> out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find(',', pos);
        std::string piece = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        auto colon = piece.find(':');
        if (colon != std::string::npos) {
            int n = std::atoi(piece.c_str() + colon + 1);
            if (colon > 0 && n > 0) out.push_back({piece.substr(0, colon), n});          // "id:amount"; a bad amount or a missing id skips the piece
        } else if (!piece.empty()) out.push_back({piece, 1});                             // a bare id = 1 unit
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return out;
}

// 1 = the single-pool format (no "version" key), 2 = per-resource pools (a merged "stacks" list),
// 3 = the unified grid: "slots" [{slot, id, amount}] (the layout the player arranged) plus a merged "stacks" list for older builds.
constexpr int kSaveVersion = 3;

// ---- perks: a small ordered set of ids (saved as a list) ----
struct Perks {
    std::vector<std::string> ids;
    bool has(const std::string& id) const { return std::find(ids.begin(), ids.end(), id) != ids.end(); }
    bool add(const std::string& id) { if (id.empty() || has(id)) return false; ids.push_back(id); return true; }
};

} // namespace gameplay
