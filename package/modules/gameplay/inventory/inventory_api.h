#pragma once
// The cargo hold: a limited amount of ore and items, in cargo units. gameplay/inventory provides it; mining, crafting, the HUD, docking/trading
// and (later) NPC ships use the same service and the same rules.
//
//     auto* inv = eng.services.get<gameplay::IInventory>();      // null if the module is off
//     int got = inv->add("iron", 12);                            // how much fitted (partial accept when the hold is nearly full)
//     if (inv->remove("iron", 5)) { ... }                        // all or nothing
#include <string>
#include <vector>

namespace gameplay {

struct Stack { std::string id; int amount = 0; };

struct InventoryChanged { std::string id; int delta = 0; };      // delta > 0 added, < 0 removed
struct CargoFull { std::string id; int refused = 0; };           // an add did not (fully) fit: `refused` units were turned away

class IInventory {
public:
    virtual ~IInventory() = default;
    virtual float capacity() const = 0;                          // cargo units
    virtual float used() const = 0;
    virtual float free() const = 0;
    virtual int count(const std::string& id) const = 0;
    virtual int add(const std::string& id, int amount) = 0;      // returns the amount accepted (0..amount); never exceeds capacity
    virtual bool remove(const std::string& id, int amount) = 0;  // false and nothing removed if there is not enough
    virtual void stacks(std::vector<Stack>& out) const = 0;      // stable order: the order stacks were first created
    // capacity upgrades (data/cargo.json), like ship::IWarpDrive levels
    virtual int level() const = 0;
    virtual int levelCount() const = 0;
    virtual void setLevel(int level) = 0;                        // clamped; takes effect immediately
};

} // namespace gameplay
