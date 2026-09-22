# System map (MAP tab)

The third tab of the game menu (**I**, then Left/Right to MAP): a flat, top-down (world XZ plane, -Z up the screen) schematic of the whole star system. Not a 3D render. Code: `package/modules/ui/system_map/` (module `ui/system_map`, requires `ui/game_menu`); pure rules in `system_map_rules.h`, tests in `package/tests/test_system_map.cpp`.

## What it shows

| Item | Drawn as | Source |
|---|---|---|
| Sun | orange square at the centre (default view) | `world::IStarSystem::bodies()[0]` |
| Planets | a dot in the body's own colour (lifted to a readable brightness), size 3..7 px by radius, with its name, on a thin dotted ring of its real `orbitRadius` around its parent | `bodies()` positions as the star system already updated them (no orbital mechanics recomputed) |
| Moons | smaller dots (1.5..3 px). At system zoom their true offset is a fraction of a pixel, so a moon is pushed out to at least `planet size + 5 + 4 x index` px from its planet, in its real direction (schematic spread). Their orbit rings appear once they are 12+ px wide; names below a 60,000 rim | same |
| Stations | small green diamonds (the radar's convention), pushed out of their planet's marker the same way | `world::IStations` |
| Anomalies | a small violet cross (the radar's star colour, 4 px arms), only for DETECTED sites (Anomaly Scanner perk + in range + not investigated), exactly like the radar; hover says "Anomaly - unidentified signal". No height on the flat map. Nothing without the perk or with world/anomalies off | `world::IAnomalies` (optional, docs/ANOMALIES.md) |
| Asteroid belt | a faint brown band of 5 dotted rings | `world::IAsteroids` exposes no belt geometry, only positions: the band is measured once from the asteroids' distances to the sun (`sysmap::beltBand`: min/max within 5,000 of the median; the 1,500-rock belt outnumbers the 240 cluster rocks). With several belts only the median one is shown |
| Ship | a yellow square with a white centre | `ship::IShip` (ship_core or fake_ship) |
| Dominant body / SOI | a blue halo on the body whose sphere of influence the ship is in, plus that SOI circle (not for the sun); the info line names it | `ship/gravity` has no service, so while it is loaded the map recomputes the same choice read-only with `gravity::buildSources` / `dominantBody` from `gravity_rules.h` (the SOI ratio does not depend on `orbit.gravity_scale`; `gravity.sun_range` is read) |
| Range rings | very faint rings at 1,000 / 10,000 / 100,000 ... from the focus | - |

Hovering a marker writes `name (kind) - N units from the ship` into the info line (no click selection). Anything farther than the rim from the focus is not drawn.
Every world service is optional: without `world/star_system` the map says "no data (no star system)"; without stations, asteroids, ship or gravity those parts are just missing and the info line says "no station data" / "no ship data". Checked with `--disable=world/star_system`, `world/stations`, `ship/gravity`, `world/asteroids` and all of stations+gravity+asteroids+ship_core together: no crash.

## Scale

Logarithmic radial, around the view focus: a point `d` units from the focus is at
`f(d) = log1p(d / knee) / log1p(50)` of the map radius, `knee = range / 50`, clamped to 0..1 (`sysmap::radialFraction`). `range` is the zoom (units at the rim).
Same log1p idea as the cockpit radar, but its own function (no include of `radar_map.h`). Worked example at the default 400,000 rim (seed 1234): Planet 1 (46,493) at 0.49, Planet 3 (140,369) at 0.74, Planet 6 (352,200) at 0.97 - the six planets spread evenly where a linear map would put Planet 1 at 0.12. Orbit rings are projected point by point (72 dots), so rings around a body other than the focus bend correctly.

## Controls

| Input | Effect |
|---|---|
| Up / Down (`ui_up` / `ui_down`) or the `+` / `-` buttons | zoom in / out by 1.6x per press |
| `<` `^` `v` `>` buttons | pan by a quarter of the current rim (Left/Right keys stay the menu's tab switch, so panning is by button) |
| SUN | back to the default view centred on the sun |
| SHIP | follow the ship, zoomed to at most a 20,000 rim |

The view is clamped (`sysmap::clampView`): the rim between `zoom_min` and `zoom_max`, the focus within `zoom_max` of the sun, NaN/inf and bad limits sanitised. Mouse wheel zoom and drag pan are not done: the input service exposes no free wheel/drag action outside the weapon binding.

## Tunables

| Key | Default | Meaning |
|---|---|---|
| `system_map.zoom_min` | 500 | the closest zoom, units at the rim |
| `system_map.zoom_max` | 1,000,000 | the farthest zoom (also the pan limit) |
| `system_map.default_zoom` | 400,000 | the zoom it opens at: the whole seed-1234 system |

## Dev flag

`--map-view=RANGE[,BODY]`: open at that rim, following body id BODY (seed 1234: 6 = Planet 4, 11 = Planet 6). With `--open-menu=FRAME,2` (MAP is tab 2):
`./space_game_v2 --display=1 --input-profile=testing --frames=120 --open-menu=30,2 --map-view=4000,11 --screenshot=logs/map.bmp --screenshot-frame=100`

## Cost

Only while the tab is open: about 16 rings x 72 tiny quads plus ~30 markers, immediate-mode like the rest of the UI; no allocation per frame except the belt measurement (once) and the info string.
