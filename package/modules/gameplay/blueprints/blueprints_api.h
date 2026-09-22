#pragma once
// Blueprints: recipes that must be unlocked before they can be crafted. gameplay/blueprints provides it; docs/BLUEPRINTS.md.
// Unlocked by EXPLORATION only (an anomaly site can grant one) - never bought or sold.
//
//     auto* bp = eng.services.get<gameplay::IBlueprints>();     // null if the module is off: then NOTHING is gated (no softlock)
//     if (bp && !bp->unlocked("missile_pack")) ...
//     bp->unlock("missile_pack");                                  // false if it was already unlocked
#include <string>
#include <vector>

namespace gameplay {

class IBlueprints {
public:
    virtual ~IBlueprints() = default;
    virtual bool unlocked(const std::string& id) const = 0;
    virtual bool unlock(const std::string& id) = 0;                  // true = newly unlocked (emits BlueprintUnlocked), false = already had it / empty id
    virtual void list(std::vector<std::string>& out) const = 0;      // sorted
    virtual std::string displayName(const std::string& id) const = 0;// data/blueprints.json "name", else the title-cased id
};

// Emitted once per blueprint, when it is first unlocked.
struct BlueprintUnlocked { std::string id; std::string name; };

} // namespace gameplay
