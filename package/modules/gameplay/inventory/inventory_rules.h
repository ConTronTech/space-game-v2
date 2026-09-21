#pragma once
// Pure cargo-hold logic: no GL, no SDL, no engine types. Unit-tested in package/tests/test_inventory.cpp.
//   * every ORE has its own hold with its own cap (data/ores.json "cargo_cap"); filling iron can never block gold, uranium or crafting
//   * crafted ITEMS (and any unknown id) share one GENERAL hold measured in volume units (items may carry a "volume", default 1)
//   * add with partial accept per pool, atomic remove, stable order
//   * upgrade levels multiply both (resource_mult, general_mult)
//   * totals for the UI: sum of everything held / sum of every cap
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>
#include "gameplay/inventory/inventory_api.h"

namespace gameplay {

// ---- levels ----
struct CargoLevel { float resourceMult = 1.0f, generalMult = 1.0f; };
inline std::vector<CargoLevel> defaultCargoLevels() { return {{1, 1}, {2, 2}, {4, 4}, {8, 8}}; }
inline int clampLevel(int level, int count) { return std::clamp(level, 0, std::max(0, count - 1)); }

// ---- the ores and their caps ----
struct ResourceDef {
    std::string id;
    int cap = 20;                 // units at level 0
    bool counted = true;          // included in the totals (ores of data/ores.json); the filler "rock" is not
};

constexpr int kDefaultResourceCap = 20;      // an ore without "cargo_cap"
constexpr int kRockCap = 100;                // the worthless filler

// The built-in table (data missing): the same numbers as data/ores.json.
inline std::vector<ResourceDef> defaultResources() {
    return {{"iron", 100, true}, {"copper", 80, true}, {"titanium", 60, true}, {"gold", 50, true}, {"cobalt", 50, true},
            {"uranium", 10, true}, {"platinum", 10, true}, {"crystal", 10, true}};
}

class Cargo {
public:
    // ---- configuration ----
    void setResources(const std::vector<ResourceDef>& r) {
        resources_ = r;
        bool hasRock = false;
        for (auto& d : resources_) if (d.id == "rock") hasRock = true;
        if (!hasRock) resources_.push_back({"rock", kRockCap, false});       // the filler always has a hold, but does not count in the totals
    }
    void setGeneralBase(float units) { generalBase_ = std::max(0.0f, units); }
    void setMultipliers(float resource, float general) { resMult_ = std::max(0.0f, resource); genMult_ = std::max(0.0f, general); }
    void setVolume(const std::string& id, float v) { volumes_[id] = std::max(0.0f, v); }
    float volumeOf(const std::string& id) const { auto it = volumes_.find(id); return it == volumes_.end() ? 1.0f : it->second; }

    // ---- pools ----
    bool isResource(const std::string& id) const { return def(id) != nullptr; }
    float generalCapacity() const { return generalBase_ * genMult_; }
    float generalUsed() const { float u = 0; for (auto& s : stacks_) if (!isResource(s.id)) u += (float)s.amount * volumeOf(s.id); return u; }
    float generalFree() const { return std::max(0.0f, generalCapacity() - generalUsed()); }
    int resourceCap(const std::string& id) const { const ResourceDef* d = def(id); return d ? (int)std::floor((double)d->cap * resMult_ + 1e-6) : 0; }
    // capacity / free of the pool an id lives in (units of that pool: ore units, or volume units for the general hold)
    float capacity(const std::string& id) const { return isResource(id) ? (float)resourceCap(id) : generalCapacity(); }
    float used(const std::string& id) const { return isResource(id) ? (float)count(id) : generalUsed(); }
    float free(const std::string& id) const { return std::max(0.0f, capacity(id) - used(id)); }

    // ---- totals ----
    float totalUsed() const {
        float u = generalUsed();
        for (auto& s : stacks_) { const ResourceDef* d = def(s.id); if (d && d->counted) u += (float)s.amount; }
        return u;
    }
    float totalCapacity() const {
        float c = generalCapacity();
        for (auto& d : resources_) if (d.counted) c += (float)resourceCap(d.id);
        return c;
    }
    void countedResources(std::vector<std::string>& out) const { out.clear(); for (auto& d : resources_) if (d.counted) out.push_back(d.id); }

    // ---- contents ----
    int count(const std::string& id) const { for (auto& s : stacks_) if (s.id == id) return s.amount; return 0; }
    const std::vector<Stack>& stacks() const { return stacks_; }

    // Adds up to `amount` into the id's OWN pool; returns how much fitted (0 for amount <= 0, an empty id or a full pool). Never goes over that pool's room.
    int add(const std::string& id, int amount) {
        if (amount <= 0 || id.empty()) return 0;
        int fit;
        if (isResource(id)) fit = std::min(amount, std::max(0, resourceCap(id) - count(id)));
        else {
            float vol = volumeOf(id);
            fit = vol > 0.0f ? (int)std::min<double>(amount, std::floor((double)generalFree() / vol + 1e-6)) : amount;      // a zero-volume item always fits
        }
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

    // Replaces the contents (a loaded game). Each stack goes into its pool; bad entries (empty id, amount <= 0) are dropped, duplicates merged, and anything
    // over a pool's room is CLIPPED (returned in `clipped` so the caller can log it). Never crashes on bad data.
    void assign(const std::vector<Stack>& in, std::vector<Stack>* clipped = nullptr) {
        stacks_.clear();
        if (clipped) clipped->clear();
        for (auto& s : in) {
            if (s.id.empty() || s.amount <= 0) continue;
            int got = add(s.id, s.amount);
            if (got < s.amount && clipped) clipped->push_back({s.id, s.amount - got});
        }
    }

private:
    const ResourceDef* def(const std::string& id) const { for (auto& d : resources_) if (d.id == id) return &d; return nullptr; }

    std::vector<ResourceDef> resources_;
    float generalBase_ = 30.0f, resMult_ = 1.0f, genMult_ = 1.0f;
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

constexpr int kSaveVersion = 2;              // 1 = the single-pool format (no "version" key), 2 = per-resource pools (same stack list, loaded through the pools)

} // namespace gameplay
