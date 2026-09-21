# Warp drive (`ship/warp_drive`)

Toggle a fuel-burning speed boost along the ship's heading. The module drives the ship only through `ship::IShip`
(`consumeWarpFuel`, `setVelocity`, `setWarping`); the maths is in `warp_rules.h` (no SDL/GL/engine types, tested by `package/tests/test_warp.cpp`).

## Behaviour
- **Toggle:** input action `toggle_warp` (key **Z**), read with `IInput::pressed` in `onUpdate`. **Ignored while the game is paused** (pause menu open; a `LOG_D` line, no state change; this includes the dev flags). Engages when the ship is alive, not already engaged and has at least `warp.min_fuel`; otherwise it disengages (or logs why it cannot engage). One `LOG_I` line per change.
- **While engaged** (fixed update, so it freezes while paused): burns `warp.fuel_drain` fuel per second via `consumeWarpFuel`, and accelerates along `forward()` by `warp.speed * warp.accel_factor * dt` per step, with total speed clamped to `warp.speed`. No passive fuel regeneration (like the old game).
- **Auto-disengage:** fuel cannot cover a step (the last bit is burned, so the tank ends at exactly 0 and `FuelEmpty` fires), the ship dies, or the module shuts down. Every disengage calls `setWarping(false)`, which `ship_core` mirrors into `ShipStatus::warping` (the cockpit shows DRIVE ENGAGED / STANDBY from it).
- **Exit speed (design choice, change it if you like):** on disengage the speed is cut to `warp.exit_speed` (default 200 m/s, what the old game did), keeping the direction. Set `warp.exit_speed` to `0` for pure Newtonian behaviour: you keep the full warp momentum after leaving warp.
- Collisions at warp speed use the existing swept physics and speed-scaled damage; a crash at 2000 m/s is fatal by design.

- **Ship lookup:** `IShip` is looked up each time (never cached), so `--fake-ship` is the ship being driven, and shutdown is safe when the service is already gone. With no `IShip` at all the module logs one warning at init and does nothing.

## Tunables (`config/game.json`)
`warp.speed` 2000 (m/s), `warp.accel_factor` 2.0 (accel = speed x factor), `warp.fuel_drain` 3.33 (fuel/s, ~30 s on a full tank), `warp.min_fuel` 1.0, `warp.exit_speed` 200 (0 = keep momentum).

## Dev flags
`--auto-warp=FRAME` acts as if `toggle_warp` was pressed on that engine frame; `--auto-warp-off=FRAME` likewise (both are a toggle). Only active when given. Example (no keyboard):
`SDL_AUDIODRIVER=dummy ./space_game_v2 --frames=240 --auto-warp=40 --screenshot=out.bmp --screenshot-frame=100`

## Using it from other modules
Read `ship->status().warping` (set by the drive) and `status().warpFuel`. Refuel with `ship->addWarpFuel(x)`. Other modules never need to call the drive; to force it off, kill the ship or drain the fuel. Docking/orbit lock should check `status().warping`.
