# Warp drive (`ship/warp_drive`)

Toggle a fuel-burning speed boost along the ship's heading. The module drives the ship only through `ship::IShip`
(`consumeWarpFuel`, `setVelocity`, `setWarping`); the maths is in `warp_rules.h` (no SDL/GL/engine types, tested by `package/tests/test_warp.cpp`).

## Behaviour
- **Toggle:** input action `toggle_warp` (key **Z**), read with `IInput::pressed` in `onUpdate`. **Ignored while the game is paused** (pause menu open; a `LOG_D` line, no state change; this includes the dev flags). Engages when the ship is alive, not already engaged and has at least `warp.min_fuel`; otherwise it disengages (or logs why it cannot engage). One `LOG_I` line per change.
- **While engaged** (fixed update, so it freezes while paused): burns `warp.fuel_drain` fuel per second via `consumeWarpFuel`, and accelerates along `forward()` by `speed * warp.accel_factor * dt` per step (speed = the effective warp speed of the upgrade level), with total speed clamped to it. From rest it takes `1 / accel_factor` = **2.5 s** to reach full speed. No passive fuel regeneration (like the old game).
- **Auto-disengage:** fuel cannot cover a step (the last bit is burned, so the tank ends at exactly 0 and `FuelEmpty` fires), the ship dies, or the module shuts down. Every disengage calls `setWarping(false)`, which `ship_core` mirrors into `ShipStatus::warping` (the cockpit shows DRIVE ENGAGED / STANDBY from it).
- **Exit speed (design choice, change it if you like):** on disengage the speed is cut to `warp.exit_speed` (default 200 m/s, what the old game did), keeping the direction. Set `warp.exit_speed` to `0` for pure Newtonian behaviour: you keep the full warp momentum after leaving warp.
- Collisions at warp speed use the existing swept physics and speed-scaled damage; a crash at 5000 m/s is fatal by design. Checked with saved-game scenarios at the base drive and at level 3 (15 km/s, 250 units per step): a planet (radius 408), the sun and a 3-unit asteroid are all hit, no tunnelling (see below).

- **Ship lookup:** `IShip` is looked up each time (never cached), so `--fake-ship` is the ship being driven, and shutdown is safe when the service is already gone. With no `IShip` at all the module logs one warning at init and does nothing.

## Speed, range and normal flight
Normal flight has no drag and no speed cap (`flight.max_speed` 0, thrust 40 m/s^2), so plain thrust reaches 2,000 m/s after 50 s and 5,000 m/s after 125 s. Warp is clearly faster in *time*: 5 km/s in 2.5 s. `ship_core`'s flight feel was not changed.
One full tank (100 fuel at 3.33 fuel/s = 30 s) carries the ship **143,876 units** at level 0 (measured in the game: (0,0,0) to (0,0,-143876); the formula `tankRange` agrees), about 40% of the star system's width (the outer planet is ~350,000 from the sun).

## Upgrade levels
The effective drive is the base tunables times the level's multipliers, from `data/warp_drive.json` (read through `core::IData`; built-in defaults when the file is missing). Level 0 is the base drive.

| Level | Name | Speed | Time to full speed | Fuel per second | Tank range |
|---|---|---|---|---|---|
| 0 | Standard drive | 5,000 m/s | 2.5 s | 3.33 | 144 km |
| 1 | Tuned drive | 7,500 m/s | 2.1 s | 2.66 | 274 km |
| 2 | Heavy drive | 10,000 m/s | 1.7 s | 2.08 | 472 km |
| 3 | Deep-space drive | 15,000 m/s | 1.25 s | 1.67 | 892 km |

(`speed_mult` 1 / 1.5 / 2 / 3, `accel_mult` 1 / 1.2 / 1.5 / 2, `fuel_efficiency` 1 / 1.25 / 1.6 / 2.) Add or change levels by editing the file: entries are levels in order; any number of levels works. `warp.upgrade_level` picks the starting level; the level is saved in the game (`ship/warp_drive` key `level`, missing = keep current).
Phase 5 (inventory / crafting) raises it through the `ship::IWarpDrive` service (`warp_api.h`): `level()`, `levelCount()`, `maxSpeed()`, `setLevel(n)` (clamped, immediate), `engaged()`. The starfield's warp streaks follow `maxSpeed()`, so they reach full length at the top speed of any level.

## Tunables (`config/game.json`)
`warp.speed` 5000 (m/s, base), `warp.accel_factor` 0.4 (accel = speed x factor), `warp.fuel_drain` 3.33 (fuel/s at level 0, ~30 s on a full tank), `warp.min_fuel` 1.0, `warp.exit_speed` 200 (0 = keep momentum), `warp.upgrade_level` 0.

## Dev flags
`--auto-warp=FRAME` acts as if `toggle_warp` was pressed on that engine frame; `--auto-warp-off=FRAME` likewise (both are a toggle). Only active when given. Example (no keyboard):
`SDL_AUDIODRIVER=dummy ./space_game_v2 --frames=240 --auto-warp=40 --screenshot=out.bmp --screenshot-frame=100`
At `engine.log_level: debug` the drive logs speed and fuel twice a second while engaged, and the ENGAGED/DISENGAGED lines carry the ship position (distance per tank = the difference).

## Tunnelling check (5 km/s = 83 units per step)
Saved games at the origin aimed at each target with `--auto-warp=40`: planet 1 (aimed with the orbit prediction) `hit planet (radius 408) at 5003.2 m/s closing: damage 1250.8`; the sun `hit sun ... at 5000.0 m/s closing`; a radius-3 asteroid 4,500 units ahead `hit asteroid (radius 3) at 4266.7 m/s closing` (it has not reached full speed yet). At level 3 (15 km/s): the same asteroid at 10,600 m/s and the planet at 14,745 m/s, all registered.

## Using it from other modules
Read `ship->status().warping` (set by the drive) and `status().warpFuel`. Refuel with `ship->addWarpFuel(x)`. Other modules never need to call the drive; to force it off, kill the ship or drain the fuel. Docking/orbit lock should check `status().warping`.
