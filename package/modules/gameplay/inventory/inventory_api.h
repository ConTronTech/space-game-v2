#pragma once
// The cargo hold: ONE unified grid of slots (default 8 x 12 = 96) shared by ores and crafted items. Every id stacks up to its own STACK CAP
// (ores: data/ores.json cargo_cap, items: data/items.json stack_cap), multiplied by the cargo upgrade level; the slot count never changes.
// gameplay/inventory provides it; mining, crafting, the HUD, the CARGO grid, docking/trading and (later) NPC ships use the same service and rules.
//
//     auto* inv = eng.services.get<gameplay::IInventory>();      // null if the module is off
//     int got = inv->add("iron", 12);                            // auto-stacks: same-id slots first, then empty slots; partial accept when nearly full
//     if (inv->remove("iron", 5)) { ... }                        // all or nothing, across every iron slot
//     inv->free("uranium");                                      // how many more uranium units fit right now
//     inv->used(); inv->capacity();                              // the TOTALS, in SLOTS: "CARGO 12 / 96"
#include <string>
#include <vector>

namespace gameplay {

struct Stack { std::string id; int amount = 0; };                // also one grid slot (an empty slot has an empty id)

struct InventoryChanged { std::string id; int delta = 0; };      // delta > 0 added, < 0 removed
struct CargoFull { std::string id; int refused = 0; };           // an add did not (fully) fit into the grid; `refused` units were turned away

class IInventory {
public:
    virtual ~IInventory() = default;

    // ---- totals, in grid SLOTS (what the HUD and the menu show: "CARGO 12 / 96") ----
    virtual float capacity() const = 0;                          // slot count (fixed; the upgrade level does not change it)
    virtual float used() const = 0;                              // occupied slots
    virtual float free() const = 0;                              // empty slots
    // ---- one id, in UNITS of that id ----
    virtual float capacity(const std::string& id) const { (void)id; return capacity(); }   // count(id) + free(id): the most of it the hold could carry right now
    virtual float free(const std::string& id) const { (void)id; return free(); }           // room: partial same-id slots + every empty slot at a full stack
    virtual bool isResource(const std::string& id) const { (void)id; return true; }       // true = an ore (data/ores.json, or the filler "rock"); false = an item / raw id
    virtual void resources(std::vector<std::string>& out) const { out.clear(); }           // the ore ids, in data/ores.json order (rock not listed)

    virtual int count(const std::string& id) const = 0;          // summed across every slot of the id
    virtual int add(const std::string& id, int amount) = 0;      // returns the amount accepted (0..amount); auto-stacks, then spills to empty slots
    virtual bool remove(const std::string& id, int amount) = 0;  // false and nothing removed if there is not enough (taken from the last slots first)
    virtual void stacks(std::vector<Stack>& out) const = 0;      // one entry per id (summed), in the order the ids first appear in the grid
    // capacity upgrades (data/cargo.json): a level multiplies every id's STACK CAP ("stack_mult"); the slot count stays fixed
    virtual int level() const = 0;
    virtual int levelCount() const = 0;
    virtual void setLevel(int level) = 0;                        // clamped; takes effect immediately (a lowered level keeps over-full slots)
    // perks: permanent flags bought with items (saved). "ore_scanner" = the Ore Scanner is installed (the HUD / radar shows ore labels).
    virtual bool hasPerk(const std::string& id) const { (void)id; return false; }
    virtual bool addPerk(const std::string& id) { (void)id; return false; }   // false if it was already set

    // ---- the grid (the CARGO tab) ----
    virtual int slotCount() const { return 0; }
    virtual int gridColumns() const { return 8; }                // rows = slotCount() / gridColumns()
    virtual void slots(std::vector<Stack>& out) const { out.clear(); }         // slotCount() entries, in grid order; an empty slot has an empty id
    virtual int stackCap(const std::string& id) const { (void)id; return 0; }  // units per slot of that id at the current level
    virtual int stackCapAtLevel(const std::string& id, int level) const { (void)id; (void)level; return 0; }   // for "next level: 60 per stack"
    virtual int discardSlot(int slot, int amount) { (void)slot; (void)amount; return 0; }   // amount <= 0 = the whole slot; returns units discarded (emits InventoryChanged)
    virtual bool moveSlot(int from, int to) { (void)from; (void)to; return false; }         // onto empty = move, same id = merge up to the cap, other id = swap
    // Room for `id` once `removed` has been taken out (crafting: can the result fit after its ingredients are consumed?). Changes nothing.
    virtual int roomAfter(const std::vector<Stack>& removed, const std::string& id) const { (void)removed; return (int)free(id); }
};

} // namespace gameplay
