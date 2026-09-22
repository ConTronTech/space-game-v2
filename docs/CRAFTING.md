# Crafting (`gameplay/crafting`)

Ore becomes items, and items become fuel, repairs and upgrades. **Fuel, shield and repair are never free** (docking gives nothing by default): everything comes from mined ore (docs/MINING.md) through the cargo hold (docs/INVENTORY.md).
Code: `package/modules/gameplay/crafting/` (`crafting_rules.h` is pure and unit-tested by `test_crafting.cpp`). The UI is the CRAFTING tab of the game menu (docs/GAME_MENU.md) and the USE buttons of the CARGO tab.

## Recipes (`data/recipes.json`)
`"id": { "name": ..., "result": <item id from items.json>, "ingredients": { <ore or item id>: amount, ... } }`, optional `"requires_blueprint": "<blueprint id>"` (docs/BLUEPRINTS.md; Missile Pack is gated). Adding a recipe is adding an entry (a unit test checks every recipe makes a real item from real ores).
**`craft(id)`** is atomic: it works only if **all** ingredients are in the hold **and** the result fits (ingredients leave first, so their space counts as free; the result's volume is the optional `"volume"` in `data/items.json`, default 1), otherwise nothing changes. Refusal reasons: `blueprint required: <name>` (checked first; only while `gameplay/blueprints` is loaded, docs/BLUEPRINTS.md), `missing N <ore>` (the first missing ingredient), `cargo full`, `dock at a station to craft` (only with `crafting.require_dock`), `unknown recipe`, `no cargo hold`.

## Items and effects (`data/items.json`, `effect`)
`use(item)` applies the effect **only through `ship::IShip`** and consumes one item **only after** it has been applied. A refused use keeps the item. Effect table:

| Effect key | What it does | Refused when |
|---|---|---|
| `warp_fuel` | `IShip::addWarpFuel` (clamped at the tank size) | warp fuel already full |
| `hp` | `IShip::heal` | hull already at maximum |
| `hp_full` | heal to maximum | hull already at maximum |
| `max_hp` + `max_hp_cap` | `IShip::addMaxHp` (permanent, up to the cap, 200) | max HP already at the cap |
| `shield_enabled` | `IShip::installShield(true)` (permanent) | a shield is already installed |
| `missiles` | `combat::IAmmo::addMissiles(n)` (rack cap `combat.max_missiles`, 12; a nearly full rack takes what fits) | **missile rack full** (the pack is not consumed); `no missile rack` when `combat.enabled` is false |
| `hud_ore_labels` | sets the inventory perk `ore_scanner` (`IInventory::addPerk`, saved; permanent) | **ore scanner already installed**. With the perk the radar dots take ore colours, the lock info line shows the ore and expected yield and the lock bracket is ore-tinted (docs/MINING.md "Ore Scanner") |
| `anomaly_scan` | sets the inventory perk `anomaly_scanner` (saved; permanent): anomaly sites show on the radar and can be investigated (docs/ANOMALIES.md) | **anomaly scanner already installed** |

Also refused: `ship destroyed`, `not in cargo`, `this item has no effect`, `no ship`. An item with an applicable and a refused effect applies the applicable one. Permanent items apply one at a time (the shield generator refuses a second one; hull plating keeps working until the cap).
The Missile Pack (+3 missiles, docs/COMBAT.md) and the Ore Scanner work since 4.2b; `decideUse` gets the rack (`ShipState::missiles/maxMissiles`) and the perk (`ShipState::oreScanner`) and returns `UsePlan::addMissiles` / `setOreScanner`. The Anomaly Scanner (crystal 6, platinum 3, gold 3) mirrors it: `Effect::anomalyScan`, `ShipState::anomalyScanner`, `UsePlan::setAnomalyScanner`.

## The hard-game switch
`crafting.require_dock` (default **true**, user decision 2026-09-21: a docked station is the workbench; a setting either way for testing/modding): crafting works only while `ship::IDocking::docked()`; the tab shows "Dock at a station to craft" and every button is greyed with that reason. `false` crafts anywhere.

## Service, events, save
`gameplay::ICrafting` (`crafting_api.h`): `recipes(out)` (with have/need per ingredient and the verdict), `canCraft(id, reason)`, `craft(id, reason)`, `usable(item)`, `use(item, reason)`, `requiresDock()`, `message()` / `messageOk()` / `messageAge()` (the last result line for the UI).
Events: `gameplay::CraftResult{recipe, ok, reason}`, `gameplay::ItemUsed{item}`. Sound: plays `ui_confirm` if `core/audio` has it, else silent.
**Saving:** crafting keeps no state. The hold is saved by the inventory; shield and max HP by `ship_core`.

## The CRAFTING tab
One row per recipe: name, ingredients as `Name have/need` (**green** enough, **red** missing), a CRAFT button that is greyed out (clicks ignored) with the reason next to it when the recipe cannot be crafted. The result of the last craft / use is shown at the bottom for 8 s.

## Tunables
`crafting.require_dock` (true).

## Cargo rework
Ingredients are taken from the resource holds; the result needs room only in the GENERAL hold (its `volume`). Ingredients that live in the general hold free their own volume first. Refusal reason: "general hold full (use or discard an item)". Discard from the CARGO tab (see GAME_MENU.md) to make room, so crafting can never softlock.
