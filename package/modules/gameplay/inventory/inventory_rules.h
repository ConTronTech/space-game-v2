#pragma once
// Pure cargo-hold logic: no GL, no SDL, no engine types. Unit-tested in package/tests/test_inventory.cpp.
//   * stacks keyed by item id (ores and items share one namespace; an unknown id is just a raw stack)
//   * capacity in cargo units; every unit of an item takes `volume` cargo units (ores 1, items default 1)
//   * add with partial accept, atomic remove, stable order
//   * capacity levels (multipliers on the base capacity)
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>
#include "gameplay/inventory/inventory_api.h"

namespace gameplay {

struct CargoLevel { float capacityMult = 1.0f; };
inline std::vector<CargoLevel> defaultCargoLevels() { return {{1.0f}, {2.0f}, {4.0f}, {8.0f}}; }     // 100 / 200 / 400 / 800 at the default base

inline int clampLevel(int level, int count) { return std::clamp(level, 0, std::max(0, count - 1)); }

class Cargo {
public:
    void setCapacity(float c) { capacity_ = std::max(0.0f, c); }
    float capacity() const { return capacity_; }
    void setVolume(const std::string& id, float v) { volumes_[id] = std::max(0.0f, v); }
    float volumeOf(const std::string& id) const { auto it = volumes_.find(id); return it == volumes_.end() ? 1.0f : it->second; }

    float used() const { float u = 0; for (auto& s : stacks_) u += (float)s.amount * volumeOf(s.id); return u; }
    float free() const { return std::max(0.0f, capacity_ - used()); }
    int count(const std::string& id) const { for (auto& s : stacks_) if (s.id == id) return s.amount; return 0; }
    const std::vector<Stack>& stacks() const { return stacks_; }

    // Adds up to `amount`; returns how much fitted (0 for amount <= 0, an empty id or a full hold). Never goes over capacity. A zero-volume item always fits.
    int add(const std::string& id, int amount) {
        if (amount <= 0 || id.empty()) return 0;
        float vol = volumeOf(id);
        int fit = amount;
        if (vol > 0.0f) fit = (int)std::min<double>(amount, std::floor((double)free() / vol + 1e-6));
        if (fit <= 0) return 0;
        for (auto& s : stacks_) if (s.id == id) { s.amount += fit; return fit; }
        stacks_.push_back({id, fit});
        return fit;
    }

    // All or nothing. A stack that reaches 0 disappears.
    bool remove(const std::string& id, int amount) {
        if (amount <= 0) return false;
        for (size_t i = 0; i < stacks_.size(); i++) {
            if (stacks_[i].id != id) continue;
            if (stacks_[i].amount < amount) return false;
            stacks_[i].amount -= amount;
            if (stacks_[i].amount == 0) stacks_.erase(stacks_.begin() + (long)i);
            return true;
        }
        return false;
    }

    void clear() { stacks_.clear(); }

    // Replaces the contents (a loaded game): bad entries (empty id, amount <= 0) are dropped, duplicates merged. The hold is NOT clamped to capacity:
    // a save made with a bigger hold keeps its cargo (free() is just 0 until the hold is emptied or upgraded).
    void assign(const std::vector<Stack>& in) {
        stacks_.clear();
        for (auto& s : in) {
            if (s.id.empty() || s.amount <= 0) continue;
            bool merged = false;
            for (auto& t : stacks_) if (t.id == s.id) { t.amount += s.amount; merged = true; }
            if (!merged) stacks_.push_back(s);
        }
    }

private:
    float capacity_ = 100.0f;
    std::vector<Stack> stacks_;
    std::unordered_map<std::string, float> volumes_;
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

} // namespace gameplay
