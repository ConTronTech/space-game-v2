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
