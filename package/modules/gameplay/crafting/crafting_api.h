#pragma once
// Crafting and using items. gameplay/crafting provides it; the game menu's CRAFTING and CARGO tabs use it, and so can anything else
// (a station shop, an NPC crafter) under the same rules.
//
//     auto* c = eng.services.get<gameplay::ICrafting>();          // null if the module is off
//     std::string why;
//     if (c->canCraft("warp_fuel_cell", why)) c->craft("warp_fuel_cell", why);      // atomic: all ingredients + room for the result, or nothing
//     c->use("warp_fuel_cell", why);                                                  // applies the effect through IShip; refuses (and keeps the item) when it would do nothing
#include <string>
#include <vector>

namespace gameplay {

struct IngredientInfo { std::string id; std::string name; int need = 0; int have = 0; };
struct RecipeInfo {
    std::string id, name, result;
    std::vector<IngredientInfo> ingredients;     // `have` is filled in with the current cargo count
    bool craftable = false;                      // canCraft() right now
    std::string reason;                          // why not, when !craftable ("missing 3 uranium", "cargo full", "dock at a station to craft")
};

struct CraftResult { std::string recipe; bool ok = false; std::string reason; };
struct ItemUsed { std::string item; };

class ICrafting {
public:
    virtual ~ICrafting() = default;
    virtual void recipes(std::vector<RecipeInfo>& out) const = 0;                   // in data order, with have/need and the current verdict
    virtual bool canCraft(const std::string& id, std::string& reason) const = 0;
    virtual bool craft(const std::string& id, std::string& reason) = 0;             // false = refused (nothing changed); `reason` says why either way
    virtual bool usable(const std::string& itemId) const = 0;                       // the item has an effect (a USE button makes sense)
    virtual bool use(const std::string& itemId, std::string& reason) = 0;           // false = refused, the item is NOT consumed
    virtual bool requiresDock() const = 0;                                          // crafting.require_dock
    // the last result line for the UI ("Crafted Warp Fuel Cell", "Cannot use: warp fuel already full")
    virtual const std::string& message() const = 0;
    virtual bool messageOk() const = 0;
    virtual double messageAge() const = 0;                                          // seconds since it was set
};

} // namespace gameplay
