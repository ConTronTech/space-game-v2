# Anomalies + Anomaly Scanner (`world/anomalies`, Phase 6.1)

The "what's over there?" hook (docs/GAME_LOOPS.md item 1). A handful of seeded points of interest sit in the star system. They are **invisible**
until the player crafts and uses an **Anomaly Scanner** (a permanent perk, the same pattern as the Ore Scanner). With the perk, a site inside its
detection range shows on the cockpit radar as a pulsing violet star; flying right up to it **investigates** it once: ore into the hold, a
flavour-text toast (`ui/toast`, its first real caller), an event, and the site is gone for good (saved).

## Module
`package/modules/world/anomalies/`: `anomalies.cpp` (module, `world::IAnomalies`, `core::ISaveable`), `anomalies_api.h` (service + event),
`anomaly_rules.h` (pure: data parsing, generation, range tests, reward roll, saved-id helpers; unit-tested in `package/tests/test_anomalies.cpp`).
- Requires `world/star_system`. Optional: `core/data_registry`, `core/save_system`, `gameplay/inventory`, `ui/toast`, `ship/ship_core`,
  `world/asteroids`, `world/stations`, `gameplay/blueprints`. Each missing one just does less: no data = one built-in kind; no inventory = the site is still marked
  investigated and a warning says nothing was given; no toast = log + event only; no ship = nothing is detected.
- Delete the folder and the game runs as before (the radar code only looks the service up).

## Generation (deterministic from `world.seed`)
`generateAnomalies` uses the SAME `world.seed` the star system uses (mixed with a constant), so one seed always gives the same layout.
`anomalies.count` sites; zones rotate **deep space -> near a body -> in the belt** (no asteroids = deep space / near body only):
- **Deep space**: between two adjacent planet orbits (35-65% of the gap), near the orbital plane; a fixed world position.
- **Near a body**: a random planet or moon, 1.6-3x its radius + 150 units from its centre, kept 400 units off any station ring and clear of moon
  rings (a moon's site stays well inside its own orbit); the site **moves with the body** (stored as an offset from the body centre). No clear spot in
  8 tries = deep space instead.
- **Belt**: 150-400 units off one of 64 evenly-sampled asteroid positions (asteroids are static); a fixed world position.
- Kind: weighted by the kind's `weight` (all zero = uniform). The site id is its index (what the save stores).
Clearance is best-effort (checked at t = 0 in the tests), not collision avoidance.

## Detection and investigation (`anomaly_rules.h`)
- Without the `anomaly_scanner` perk nothing is detected or investigated (sites are inert).
- Detected (on the radar): distance <= `anomalies.detect_range` x the kind's `detect_mult` (clamped 0.1..5), not yet investigated.
- Investigated: distance <= `anomalies.investigate_range` (75 units: you must actually fly there). Once per site.
- Reward: `rollAnomaly(kind, site seed)` picks a reward entry (weighted) and an amount in its [min, max], and a flavour message: fixed per site, never
  re-rolled. The amount goes to `IInventory::add` (a full hold accepts less; the toast says `(hold full)`).
- Emits `world::AnomalyInvestigated{siteId, kind, oreGiven, amount}` (amount = what the hold accepted); logs `AnomalyInvestigated: site N (...)`.
- Blueprint reward (optional, docs/BLUEPRINTS.md): a kind with `"blueprint": "<id>"` unlocks it through `gameplay::IBlueprints` (optional
  dependency; absent = not granted, the site is still investigated), sets `AnomalyInvestigated::blueprint`, and adds an amber toast
  `Blueprint unlocked: <name>`.
- Toast (Info, 7 s): `Derelict Wreckage: Twisted hull plates and a cold reactor. Someone never made it home.  +6 COBALT`.

## Data: `data/anomalies.json`
```json
"energy_signature": { "name": "Energy Signature",
  "messages": ["A pulsing knot of energy collapses ...", "The readings spike, then fall silent ..."],
  "rewards": [{ "ore": "crystal", "min": 3, "max": 6, "weight": 2 }, { "ore": "uranium", "min": 2, "max": 5, "weight": 1 }],
  "detect_mult": 1.6, "weight": 2 }
```
Missing / bad fields: `name` = the id; no `messages` = `message` or "Anomaly investigated."; a reward without `ore` is dropped, `min` >= 1,
`max` >= `min` (<= 1000), `weight` >= 0; `detect_mult` default 1 clamped 0.1..5; kind `weight` default 1, >= 0. Shipped kinds: derelict_wreckage,
energy_signature (loud, x1.6), ancient_artifact (quiet, x0.6, platinum/gold), unstable_phenomenon, ancient_blueprint_cache (`"blueprint": "missile_pack"`
+ a little cobalt). Optional `blueprint` = a blueprint id (blank / non-string = none).

## The scanner item
`data/items.json` `anomaly_scanner` (permanent, `effect: {"anomaly_scan": true}`), recipe crystal 6 + platinum 3 + gold 3 (Ore Scanner tier).
Using it sets the inventory perk `anomaly_scanner` (docs/CRAFTING.md); a second one is refused (`anomaly scanner already installed`).

## Radar
Detected sites: a violet (0.85, 0.35, 1.0) 4-point star with a hollow diamond, pulsing size, same log range as bodies, with the same height stem
bodies/stations get (`radarPlot`; done 2026-09-22, w48), at most 16 looked at. See docs/COCKPIT.md. Cost: 3 canvas primitives per detected site inside the existing cached screen batch (no extra
draw call, no allocation); measured `cockpit:screens` 0.130 ms with a site shown vs 0.129 ms without (`--profile`, noise). The module's per-frame
work is 8 distance tests.

## System map
Detected sites also show on the MAP tab as a small violet cross (docs/SYSTEM_MAP.md; done 2026-09-22, w48). Same rule: nothing without the perk.
Verified with a save 2500 below and 1500 short of site 2 (`logs/w48_mksaves.py`): radar stem visible, map marker shown; without the perk and with
`--disable=world/anomalies` neither shows, no crash.

## Save (`world/anomalies`)
`{"investigated": [2, 5]}`: only which ids are done. On load the sites are already regenerated from the seed; the ids are applied
(out-of-range / duplicate ids ignored: `investigatedMask`). Missing key = none investigated.

## Tunables (`config/game.json`)
| key | default | |
|---|---|---|
| `anomalies.enabled` | true | off = no sites, no service |
| `anomalies.count` | 8 | 0..64 |
| `anomalies.detect_range` | 4000 | units, x kind `detect_mult` |
| `anomalies.investigate_range` | 75 | units |

## Dev flags and testing
- `--give-anomaly-scanner`: sets the perk without crafting (also kept locally, so it survives loading a save without it).
- `--anomaly-dump`: logs every site: `site 2: derelict_wreckage (belt) at (92027, 271, -149096)`.
- Scenario (seed 1234): saved games built by `logs/w47_mksaves.py` put the ship 2500 / 400 units from site 2; load with
  `--resolution=800x600 --paused=load --ui-click=400,297,20 --saves=<dir>`, cockpit camera via a `--settings=` copy with `camera.mode` 0.
  Verified 2026-09-22: without the perk nothing shows / no investigation; with it the star shows at 2500 units, flying in logs
  `AnomalyInvestigated: site 2 (derelict_wreckage): +6 cobalt`, cargo 6, the toast, the marker gone; a save with `"investigated":[2]` loads
  `1 of 8 sites investigated` and flying through does nothing.
