# Ship HUD (`ui/ship_hud`)

The flight HUD. It reads the ship **only** through `ship::IShip` (`ship/ship_core/ship_api.h`) and the events `DamageTaken`,
`ShieldBroken`, `FuelEmpty`, `Respawned`. If no `IShip` is provided (ship module off) it draws nothing and logs **one** warning.

## What is on screen
| Element | Where | Notes |
|---|---|---|
| Speed | top-left panel | `status().speed`, m/s |
| HULL / SHLD / WARP bars | top-left, under speed | SHLD only when `shieldInstalled`. Hull: green -> amber (50%) -> red. Shield (blue) and warp fuel (violet) keep their colour to 30%, then amber -> red |
| Crosshair | centre | hidden when dead |
| Controls hint | bottom centre | shrinks its font to fit narrow windows |
| IMPACT banner + faint red edge | top centre | on `DamageTaken` (amount that reached the hull + source), fades over 0.7 s |
| Warning banners | top centre, stacked | flashing glass. LOW HULL INTEGRITY (hp <= 25%), LOW WARP FUEL (0 < fuel <= 15%), WARP FUEL EMPTY (3 s after `FuelEmpty`), SHIELD BROKEN (3 s after `ShieldBroken`) |
| Destroyed screen | full screen | `alive == false`: red vignette + "SHIP DESTROYED" (a later respawn module adds the countdown); banners are suppressed |

Everything scales with `min(w/1280, h/720)` clamped to 0.6..3, so 1024x600 .. 4K stay legible and the bar stack never reaches the hint line.

## Code map
- `hud_logic.h` - pure logic, no SDL/GL: `barColor`, `computeLayout`, `fitHint`, `HudState` (impact + banner timers, thresholds). Tests: `package/tests/test_hud_logic.cpp`.
- `ship_hud.cpp` - the module: subscribes to events, draws a UIHandler panel `ui/ship_hud` (order 10).
- `core::UIHandler` gained two **additive** widgets: `bar(x,y,w,h,frac,color)` (glass track + fill) and `vignette(color, thickness)`.

## Fake ship for development (`ship/fake_ship`)
Inert unless launched with `--fake-ship=key:value,...` (then it provides `ship::IShip`; without the flag it never does, so it coexists with `ship_core`).
Keys: `hp`, `shield` (installs the shield; `0` = broken), `fuel`, `speed`, `hit:N` (one DamageTaken of N), `dead`. Events for the state fire once on the first update.
```
./space_game_v2 --disable=gameplay/flight --fake-ship=hp:20,shield:120,fuel:10,speed:12,hit:14 --screenshot=/tmp/a.bmp --screenshot-frame=20
./space_game_v2 --fake-ship=hp:60,shield:0,fuel:80      # SHIELD BROKEN banner
./space_game_v2 --fake-ship=hp:70,fuel:0                # WARP FUEL EMPTY
./space_game_v2 --fake-ship=hp:0,dead                   # SHIP DESTROYED
```
(`--disable=gameplay/flight` hides the old demo HUD, which draws on top of this one until ship_core replaces it.)

## On the ship: cockpit overlay modes
The default setup is the cockpit ship model, whose own screens (INFO / SYSTEMS / RADAR) already show speed, hull/shield/fuel and a radar with the sun, planets and moons from `world::IStarSystem` (with height stems above/below your plane, see docs/COCKPIT.md, "Radar contacts"); the flat HUD has no radar and is unchanged.
Each frame the HUD looks up `cockpit::ICockpitScreens` (optional; `ship/cockpit`, see `ship/cockpit/cockpit_screens_api.h`).
- `showsDefaultUI()` **true** (chase view, ship model without screens, cockpit off) or no service: everything is drawn as described above, whatever the tunable says.
- `showsDefaultUI()` **false** (the ship's screens are visible): the tunable `hud.cockpit_overlay` (in `config/game.json`) decides:

| Mode | Speed + bars | Crosshair | IMPACT / warning banners | Hint | Hit vignette + SHIP DESTROYED |
|---|---|---|---|---|---|
| `minimal` (default) | hidden | yes | yes | yes (fades) | yes |
| `full` | yes | yes | yes | yes (fades) | yes |
| `hidden` | hidden | hidden | hidden | hidden | yes |

An unknown value logs one warning and uses `minimal`.

## Controls hint fade
The hint shows at full opacity for the first `hud.hint_seconds` (default 20, engine time) and then fades out over 2 s. `0` = never fade.

The decisions are pure functions in `hud_logic.h` (`parseOverlayMode`, `overlayPlan`, `hintAlpha`), unit-tested for every mode x `showsDefaultUI` combination and the fade edges.

## Respawn countdown
While the ship is destroyed and `ship::IRespawn` (`ship/respawn`, see docs/RESPAWN.md) is counting, "RESPAWNING IN N" (N = ceil of seconds left, 3, 2, 1)
is shown under SHIP DESTROYED. Without the service, or with `respawn.enabled=false`, only SHIP DESTROYED shows. Formatting: `hud::respawnText` in `hud_logic.h`.

## Orbit lock
`ui/ship_hud` subscribes to `ship::OrbitLockChanged` (from `ship/orbit_lock`, see docs/ORBIT_LOCK.md; the module is optional, without it nothing happens).
- While locked a **persistent status line** "ORBIT LOCKED: PLANET 1" (body name upper-cased; just "ORBIT LOCKED" if the name is empty) sits in the banner stack in a calm cyan, not flashing, never expiring.
- On release a short **"ORBIT RELEASED"** banner (2.5 s, fades) shows. A release with no lock before it shows nothing; a dead ship shows neither.
- Both belong to the banner group of the overlay plan: shown in chase view, `full` and `minimal` (so also in the default cockpit view, where the ship's screens have no orbit indicator), hidden in `hidden`.
- Logic: `orbitStatusText` and `HudState::onOrbitLock / orbitLocked / orbitStatus` in `hud_logic.h`.

## Station docking prompt
Optional: uses `ship::IDocking` (`ship/docking`) and, for the dock radius, `world::IStations`; without them nothing is drawn. All lines are in the banner group (chase view, `full`, and `minimal` cockpit view above the ship screens).
- **Near a station** (within `docking.prompt_range` units of its centre; default `0` = 4 x the station's dock radius, from `IStations`, 480 for a 120 zone): a calm status line "STATION 1  420 m" (short name, distance in m / km) and under it either a green "DOCK [G]" when `nearestDockable` says ok, or the reason in a dim colour ("too far", "too fast", "warp drive on", "orbit lock on"; unknown reasons as they are).
- **Docked:** a persistent "DOCKED: STATION 1 - [G] UNDOCK" line; short "DOCKED" / "UNDOCKED" banners on `ship::Docked` / `Undocked` (2.5 s, each replaces the other).
- The key label is the default profile's `G` (`core::IInput` does not expose bindings). Logic: `dockPrompt`, `shortDockReason`, `shortStationName`, `distanceText`, `HudState::onDocked/onUndocked` in `hud_logic.h`. The distance query reuses member strings, so no per-frame allocation in the draw path beyond the text the HUD already builds; the station radius is looked up only when the nearest station's name changes.

## Weapons (`combat/weapons`, optional)
Uses `combat::ICombat` (per-use get) and the events `combat::Overheated`, `combat::WeaponChanged`, optionally `world::AsteroidDestroyed`; without the weapons module none of it is drawn. Sizes are a few small quads (no full-screen layer).
- **Crosshair heat:** two columns of 8 small segments either side of the crosshair fill (bottom first) with the selected weapon's heat, coloured cyan -> amber (60%) -> red; while locked out all segments flash red; dimmed while docking (`ship::IDocking::busy()`). Hidden with the crosshair (dead, `hidden` mode).
- **Hit marker:** four L-shaped ticks around the crosshair when `hitMarkerAge()` is under 0.25 s (fades); red and 0.45 s after `AsteroidDestroyed` (a kill).
- **Weapon block** (bottom-left, above the hint, banner group so it shows in cockpit `minimal` too): one row per weapon, "BLASTER  [1]" + mini heat bar; the selected one bright, others dim; a locked-out weapon shows a flashing "OVERHEATED", and while docking/approaching the whole block is dimmed and shows "DOCKED". Key labels are `weapon_1`/`weapon_2` = 1/2 (fixed: `IInput` exposes no bindings).
- **Banners:** "BLASTER OVERHEATED" (flashing, warning colour, 2.5 s) and "WEAPON: MINING BEAM" (2 s, calm). `WeaponChanged` is only emitted when the player switches, not for `--auto-weapon`.
- While `IDocking::busy()` (approaching the pad, not yet docked) the station prompt is hidden.
- Logic: `heatColor`, `heatSegments`, `overheatFlash`, `hitMarker`, `weaponLabel`, `weaponRowState`, `HudState::onOverheated/onWeaponChanged/onEnemyKilled` in `hud_logic.h`.

## Cargo and ore pickups (`gameplay/inventory`, `gameplay/mining`, optional)
- **Cargo row:** "CARGO 45 / 100" + a thin fill bar, the first row of the bottom-left block (the same single glass panel as the weapon rows, so it costs no extra panel). Cyan up to 80 %, amber above, red at 100 %. Hidden without `gameplay::IInventory`; shown in cockpit `minimal` too (banner group).
- **Pickup banners:** on `gameplay::OreMined` a stacked "+6 CRYSTAL" banner (display name from `data/ores.json` via `core::IData`, else the id; ore colour brightened, else calm cyan), fading over 1.5 s; repeated pickups of the same ore within 0.5 s merge into one banner with the summed amount; at most 4 shown (the oldest slot is reused).
- **CARGO FULL:** on `gameplay::CargoFull` a flashing amber banner (2 s), rate-limited to one per 2 s.
- Logic (`cargoFraction/Colour/Text`, `pickupText`, `oreLabel`, `HudState::onOreMined/forEachPickup/onCargoFull`) is in `hud_logic.h`, unit-tested. Test cargo: `--give=iron:90`.

## HUD cost notes (laptop pass)
Text is cached or throttled and per-frame allocations were removed: the speed text refreshes 10x a second (a live number otherwise creates a new text texture every frame), the hint text/fit is computed once per window size, the orbit/dock lines are built once per event, cargo text once per change, ore names once per ore id, weapon labels once.
The remaining cost is dominated by `UIHandler::glass()` (5 shadow layers + fill + border per panel); the HUD keeps the number of panels low (cargo shares the weapon panel). Measure with `--profile --benchmark=8 --no-vsync`, row `core/ui_handler:ui`.

## Orbit guide block
While the orbit guide is active (`ship/orbit_lock`, docs/ORBIT_LOCK.md) a two-line status block shows at the **top right**: "ORBIT PLANET 1  alt 1.0K  need 84 m/s" and "SPEED +12  HEADING 4 deg  [ALIGNED - press O]" (green) or the refusal reason in amber. It is drawn by `ship/orbit_lock` itself as a `core::UIHandler` panel (`ship/orbit_guide`, order 12), so it works in cockpit and chase view and this module is unchanged; the top-centre banner stack stays free.
