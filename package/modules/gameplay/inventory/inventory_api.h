#pragma once
// The cargo hold: PER-RESOURCE holds (every ore has its own cap: 0-100 iron, 0-50 gold, 0-10 uranium...) plus a GENERAL hold for crafted items.
// Filling one ore can never block another ore or crafting. gameplay/inventory provides it; mining, crafting, the HUD, docking/trading and (later) NPC ships
// use the same service and the same rules.
//
//     auto* inv = eng.services.get<gameplay::IInventory>();      // null if the module is off
//     int got = inv->add("iron", 12);                            // how much fitted into THAT pool (partial accept when it is nearly full)
//     if (inv->remove("iron", 5)) { ... }                        // all or nothing (also how the player discards)
//     inv->capacity("uranium"); inv->free("uranium");            // one pool
//     inv->used(); inv->capacity();                              // the TOTALS: sum of every resource and the general hold / sum of every cap
#include <string>
#include <vector>

namespace gameplay {

struct Stack { std::string id; int amount = 0; };

struct InventoryChanged { std::string id; int delta = 0; };      // delta > 0 added, < 0 removed
struct CargoFull { std::string id; int refused = 0; };           // an add did not (fully) fit into that id's pool; `refused` units were turned away

class IInventory {
public:
    virtual ~IInventory() = default;

    // ---- totals (what the HUD and the menu show: "CARGO 233 / 340") ----
    virtual float capacity() const = 0;                          // sum of every resource cap + the general capacity (only ores in data/ores.json count)
    virtual float used() const = 0;                              // sum of everything held in those pools
    virtual float free() const = 0;                              // capacity() - used(): informational only, the pools are separate
    // ---- one pool: an ore id has its own hold, every other id (crafted items, raw ids) shares the general hold ----
    virtual float capacity(const std::string& id) const { (void)id; return capacity(); }
    virtual float free(const std::string& id) const { (void)id; return free(); }
    virtual bool isResource(const std::string& id) const { (void)id; return true; }       // true = an ore with its own hold; false = general hold
    virtual void resources(std::vector<std::string>& out) const { out.clear(); }           // the ore ids that have a hold, in data/ores.json order
    virtual float generalCapacity() const { return 0; }
    virtual float generalUsed() const { return 0; }

    virtual int count(const std::string& id) const = 0;
    virtual int add(const std::string& id, int amount) = 0;      // returns the amount accepted (0..amount); never exceeds that pool's room
    virtual bool remove(const std::string& id, int amount) = 0;  // false and nothing removed if there is not enough
    virtual void stacks(std::vector<Stack>& out) const = 0;      // stable order: the order stacks were first created
    // capacity upgrades (data/cargo.json), like ship::IWarpDrive levels: a level multiplies every resource cap AND the general hold
    virtual int level() const = 0;
    virtual int levelCount() const = 0;
    virtual void setLevel(int level) = 0;                        // clamped; takes effect immediately
};

} // namespace gameplay
