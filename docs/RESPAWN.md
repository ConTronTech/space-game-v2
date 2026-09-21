# Respawn (`ship/respawn`)

After the ship dies (`ship::IShip::status().alive == false`, for any cause: rock, sun, `kill()`), the module waits `respawn.seconds` and calls
`ship::IShip::respawn()` (spawn point, full hull and fuel, no shield; emits `Respawned`).

- It **polls** `alive` every frame in `onUpdate`, so a save that was loaded while dead also counts down.
- The countdown only advances while the game is not paused (`Engine::paused()`).
- Exactly one `respawn()` per death; a ship that comes back alive during the countdown (e.g. a loaded save) cancels it.
- With no `IShip` it does nothing and logs one warning.

## Tunables (`config/game.json`)
| Key | Default | Meaning |
|---|---|---|
| `respawn.seconds` | 3.0 | delay between destruction and respawn (minimum 0.5) |
| `respawn.enabled` | true | `false` = never respawn automatically (hardcore): the destroyed screen stays, no countdown |

## Service
`ship::IRespawn` (`respawn_api.h`): `counting()`, `secondsLeft()`. `ui/ship_hud` shows "RESPAWNING IN N" (N = ceil of the time left) under SHIP DESTROYED
when the service is present and counting.

## Code map
`respawn_rules.h` is the pure state machine (tests: `package/tests/test_respawn.cpp`); `respawn.cpp` wires it to the engine.
