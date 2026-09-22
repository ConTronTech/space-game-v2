# Blueprints (`gameplay/blueprints`, Phase 6.2)

docs/GAME_LOOPS.md item 2: some recipes need a **blueprint** the player does not start with. Blueprints are unlocked by **exploration only**
(investigating an anomaly site, docs/ANOMALIES.md) - they are **never bought or sold** (the station economy is withheld: docs/ROADMAP.md
"Parked ideas"). The basic survival recipes (Warp Fuel Cell, Fuel Canister, Repair Kit, Hull Plating) are never gated, so a new player cannot
softlock.

## Module
`package/modules/gameplay/blueprints/`: `blueprints.cpp` (module, `gameplay::IBlueprints`, `core::ISaveable`), `blueprints_api.h` (service + event),
`blueprints_rules.h` (pure: the `BlueprintSet`, its save format, id trimming and the display-name fallback; unit-tested in `package/tests/test_blueprints.cpp`).
- No required dependencies. Optional: `core/save_system` (not saved without it), `core/data_registry` (names from `data/blueprints.json`).
- `gameplay::IBlueprints`: `unlocked(id)`, `unlock(id)` (false if already unlocked or the id is blank), `list(out)` (sorted), `displayName(id)`.
- Event `gameplay::BlueprintUnlocked{id, name}` once per blueprint; log `BlueprintUnlocked: missile_pack (Missile Pack)`.
- **Absent service = no gate.** With the module disabled (`--disable=gameplay/blueprints`) or deleted, gated recipes craft like any other and
  anomaly blueprint rewards are simply not granted (same "optional service adds no restriction" rule as everywhere else).

## Display names: `data/blueprints.json`
`"missile_pack": { "name": "Missile Pack" }`. An id missing from the file (or a non-string / blank `name`) falls back to the title-cased id
(`missile_pack` -> `Missile Pack`); unlocking an id the file does not list works but logs a warning.

## How a recipe gets gated
Add `"requires_blueprint": "<blueprint id>"` to its entry in `data/recipes.json` (docs/CRAFTING.md). A non-string or blank value = no gate.
`gameplay::decideCraft` then refuses with **`blueprint required: <name>`** while `IBlueprints` is present and the id is not unlocked. It is checked
**before** docking and ingredients (neither would help). Shipped: **Missile Pack** requires `missile_pack`. The CRAFTING tab shows the reason with no
UI change (it already prints each recipe's refusal reason).

## How an anomaly grants one
An anomaly kind in `data/anomalies.json` may have `"blueprint": "<id>"` (alongside or instead of ore `rewards`). Investigating such a site calls
`IBlueprints::unlock` (through the pure `world::investigateSite`, which always marks the site done), logs `site N: blueprint 'id' unlocked`
(or `already known` / `not granted (gameplay/blueprints off)`), adds `blueprint` to `world::AnomalyInvestigated`, and shows a second, **amber**
(Warning level, 9 s) toast `Blueprint unlocked: Missile Pack` under the usual flavour/ore toast. Shipped kind: `ancient_blueprint_cache`
(weight 2, + 2-4 cobalt, grants `missile_pack`). A test checks every gated recipe's blueprint is granted by at least one kind.

## Save (`gameplay/blueprints`)
`{"unlocked": ["missile_pack"]}`. Load replaces the set; non-string / blank entries and duplicates are ignored. A save without the key keeps the
current state (the save system skips modules with no data).

## Testing
- `--give-blueprint=ID[,ID...]` (test flag): unlock at startup. `--disable=gameplay/blueprints`: no gate at all.
- Unit tests: `package/tests/test_blueprints.cpp` (set, save round trip, names, the gate locked / unlocked / absent, recipe parsing, the anomaly
  reward with and without the service, shipped data consistency).
- Scenario (seed 1234, verified 2026-09-22): `--anomaly-dump` lists `site 5: ancient_blueprint_cache (belt) at (112762, -668, -133324)`;
  `logs/w49_mksaves.py` writes saves 3000 units (still) / 400 units (60 m/s, flying in) from it with ores in the hold. With `crafting.require_dock`
  false in a local config copy, load with `--resolution=800x600 --display=1 --input-profile=testing --paused=load --ui-click=400,297,20 --saves=<dir>`,
  `--open-menu=FRAME,1` for the CRAFTING tab. Results: Missile Pack row reads `blueprint required: Missile Pack` (Repair Kit crafts); flying in with
  `--give-anomaly-scanner` logs `BlueprintUnlocked` + the amber toast; clicking CRAFT then logs `Crafted Missile Pack`; a save made with the blueprint
  holds `{"unlocked":["missile_pack"]}` and reloads as `loaded: 1 unlocked`; with `--disable=gameplay/blueprints` the site logs `not granted` and
  Missile Pack crafts.
