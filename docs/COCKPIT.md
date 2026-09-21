# Cockpit (`ship/cockpit`)

Draws the inside of the ship: an OBJ model glued to the camera (view space), plus live content on the model's custom
`@` screens. Only visible in **cockpit view** (`core::ICamera::showsShip() == false`); in chase view nothing is drawn.

```
package/modules/core/import_handler/obj_parser.{h,cpp}   pure OBJ+MTL parser (no SDL/GL), tagged '@' quads
package/modules/ship/cockpit/
  cockpit.cpp              the module: ship selection, model pass, screen loop, ICockpitScreens implementation
  cockpit_screens_api.h    ICockpitScreens + ScreenContext (the interface other modules use)
  screen_canvas.{h,cpp}    draws on one screen quad: rect, line, circle, bar, stroke-font text
  stroke_font.{h,cpp}      pure vector font (glyph table + layout), unit-tested
  hud_screens.{h,cpp}      built-in "HUD" group: FLIGHT_DATA, SHIP_SYSTEMS, PROXIMITY_RADAR (content only from ship::IShip)
  hud_layout.{h,cpp}       pure text/colour logic for those screens, unit-tested
  ship_registry.{h,cpp}    scans assets/models/ship/*/ship.json, picks the ship, unit-tested
  cockpit_light.h          light tunable parsing
package/tests/test_obj_parser.cpp, test_cockpit.cpp
```

## What it does each frame
Render pass `ship/cockpit` (order 800, after the world). In this order:
1. skip if `cockpit.enabled` is false, the model did not load, or the camera shows the ship (chase view);
2. `glLoadIdentity()` (view space: the model moves with the camera, not with the world) and **clear the depth buffer**, so the cockpit never clips into world geometry;
3. draw the opaque triangles lit by one directional light + ambient (two-sided lighting);
4. draw every `@` screen: dark backing, then the renderer registered for its group;
5. draw the translucent glass (CANOPY, 30% alpha as modelled) with depth writes off;
6. `glPopAttrib`/`glPopClientAttrib`/matrix pop: lighting, depth, blending, arrays and matrices are exactly as other passes left them.

The model is drawn as authored: the eye is the model origin, -Z is forward, +Y up, 1 unit = 1 metre (the old game did the same). Nothing is
scaled or offset. If the model does not load (or no ship exists) there is **one warning** and nothing is drawn; the game runs on.

## Ships: `assets/models/ship/<folder>/ship.json`
```json
{ "name": "ShipV2", "model": "ShipV2.obj", "showDefaultUI": false,
  "screens": [ { "tag": "@HUD-INFO", "content": "FLIGHT_DATA" }, { "tag": "@HUD-SYSTEMS", "content": "SHIP_SYSTEMS" }, { "tag": "@HUD-RADAR", "content": "PROXIMITY_RADAR" } ] }
```
| field | meaning | default |
|---|---|---|
| `name` | what `cockpit.ship` matches (case-insensitive) | the folder name |
| `model` | OBJ file in that folder (its `.mtl` is read from `mtllib`) | `ship.obj` |
| `showDefaultUI` | `false` = the model draws its own screens, the flat 2D HUD may hide (see below) | `true` |
| `screens[]` | `tag` = the `@` material name, `content` = what to show on it (passed to the group's renderer) | none |

The ship named by tunable `cockpit.ship` (default `ShipV2`) is used; if there is no such ship the first one (sorted by folder name) is used
and a warning says so. The log line `[cockpit] ship 'X' (folder/model)` tells which was chosen. `shipv1` (plain, no screens) works.

## The `@` convention (custom materials)
**Any material whose name starts with `@` is not ordinary shaded geometry.** The parser pulls its faces out as a *tagged quad*
(`core::TaggedQuad`, in `core::Mesh::tagged`); they are **not** in the triangle list, so they are never drawn as coloured mesh. Something else draws
custom content on them.

Tag format `@GROUP-NAME`: GROUP is the text before the first `-`, NAME the rest.

| material | group | name | note |
|---|---|---|---|
| `@HUD-INFO` | `HUD` | `INFO` | ShipV2 flight-data screen |
| `@HUD-SYSTEMS` | `HUD` | `SYSTEMS` | ShipV2 ship-systems screen |
| `@HUD-RADAR` | `HUD` | `RADAR` | ShipV2 radar screen |
| `@SCREEN-MAP-LEFT` | `SCREEN` | `MAP-LEFT` | only the first dash splits |
| `@GLOW` | `GLOW` | *(empty)* | no dash: whole text is the group |
| `NOTA@TAG` | – | – | `@` not first: an ordinary material |

A tagged quad carries `tag, group, name, corners[4], normal, centre, width, height` and `at(u, v)` (u 0..1 left->right, v 0..1 top->bottom).
Corners are as the modeller wrote them: bottom-left, bottom-right, top-right, top-left (counter-clockwise seen from the front).
A tagged face with more than four vertices uses the first four, a triangle is completed to a parallelogram; both add a warning. Bad faces are skipped, never a crash.
The tag needs no entry in the `.mtl`.

## Putting content on a screen: `cockpit::ICockpitScreens`
Include `ship/cockpit/cockpit_screens_api.h`. Service, so it is null when `ship/cockpit` is off:
```cpp
auto* screens = eng.services.get<cockpit::ICockpitScreens>();
if (screens) screens->registerRenderer("NAV", [](const cockpit::ScreenContext& c) {
    // c.quad (TaggedQuad), c.canvas, c.content (ship.json "content" or ""), c.time (seconds), c.brightness
    c.canvas.text(0.1f, 0.1f, "NAV " + c.quad.name, 0.12f, {0.3f, 1.0f, 0.5f});
});
// shutdown: screens->removeRenderer("NAV");
```
* One renderer per group; registering again replaces it (the built-in `"HUD"` renderer can be taken over the same way).
* The renderer runs inside the cockpit pass, right after the model, every frame the cockpit is visible. The screen already has its dark backing; blending is on, lighting off.
* **Canvas coordinates** are "screen heights": x runs `0..canvas.aspect()`, y runs `0..1` top -> bottom. Circles are round, sizes do not depend on the quad's size.
  Calls: `fill, rect, frame, line, circle, bar, text, textCentered, textWidth`. Text is the vector stroke font (A-Z, 0-9 and `+-.,:;/%!?_=()<>|'*`; lower case draws as upper).
* A group nobody renders draws a dark, blank screen.
* `showsDefaultUI()`: false only while the model's own screens are visible (ship.json `showDefaultUI=false`, has screens, cockpit drawing, cockpit view). True in chase view,
  with the cockpit off/failed, or for ships without screens. A HUD module hides its flat 2D overlay when this is false.

### Built-in HUD group
Content comes **only** through `ship::IShip` (`status()`, `forward()`). No IShip => FLIGHT_DATA and SHIP_SYSTEMS show `NO DATA` (one warning logged).
| content | shows |
|---|---|
| `FLIGHT_DATA` | speed (m/s), heading/pitch from `forward()`, HP / shield / fuel bars (green > 50%, amber > 25%, else red; shield `OFF` when not fitted/enabled) |
| `SHIP_SYSTEMS` | HULL x/max, SHIELD (not fitted / disabled / down / x/max), WARP FUEL, DRIVE standby/engaged, STATUS alive/destroyed |
| `PROXIMITY_RADAR` | top-down, forward-relative scope with the ship at the centre (ahead = up), rings at 1K / 10K / 100K units and the rim, a sweeping arm, and the contacts from `world::IStarSystem` (see below). Without `IStarSystem` (or `IShip`) it stays an empty scope with `NO CONTACTS` |

If ship.json does not name a tag, `INFO`/`SYSTEMS`/`RADAR` map to the three contents above.

### Radar contacts
`hud_screens.cpp` reads `world::IStarSystem` (optional, looked up on every draw) and `ship::IShip::position()`: each body position minus the ship position is subtracted **in double**, then converted to float and put into the ship's frame.
The frame's forward and up come from `core::ITransformSource` (the published pose); if none exists, `IShip::forward()` with world-up is used (`IShip` itself has no up/right vector).
* Plot: top-down; x = distance along the ship's right, y = along its forward. The flat position ignores height (a body straight above sits at the centre).
* **Height stems**: every contact keeps its flat position (marked by a small hollow square, the base on the ship's plane) and its dot is moved up (above the plane) or down (below) by
  `radarHeightOffset(dot(rel, ship up))`: the same logarithmic scale as the rings, at most `kMaxStem` (0.5) of the scope radius, shortened if it would leave the scope; a thin stem line joins base and dot.
  The stem is brighter when the body is above the ship's plane and dimmer when below. A body is *level* (no stem, no label) when its height is under 25 units or under 2% of its distance (~1 degree),
  so the sun at spawn does not show a meaningless 25-unit offset. Up on the scope is also "ahead", so read the base square first: dot above base = body above your plane.
* The nearest-body label gets the height: `PLANET 1  6.3K  UP 2.5K` / `... DN 2.5K` (text shrinks to fit long names).
* Range is **logarithmic** (`radarFraction` in `radar_map.h`, scale 200 units): with the default rim of 400,000 units a body 400 units away lands at ~0.14 of the radius, one 40,000 away at ~0.70. Bodies beyond `cockpit.radar_range` sit on the rim as a smaller dot.
* Dots: sun orange and big, planets in their body colour (brightened to stay visible), moons dim and small. At most the 20 nearest contacts are drawn (fixed array, no allocation).
* Under the scope: the nearest body and its distance from the **surface**, e.g. `PLANET 1  1.5K` (`850`, `1.5K`, `41.9K`, `250K`, `1.2M`).
* Asteroids are not on the radar yet: `world::IAsteroids` (docs/WORLD.md) exposes `count/position/radius/ore/nearest`, so a radar layer needs only `nearest(shipPos, N, out)` each frame. The old demo rocks (`flight.demo_rocks`, off by default) are never on it.
Pure maths (frame, mapping, height offset/level test, nearest-N list, distance and height text) is in `radar_map.h`, unit-tested in `tests/test_cockpit.cpp`.

## Tunables (`config/game.json`)
| key | default | meaning |
|---|---|---|
| `cockpit.enabled` | true | draw the cockpit at all |
| `cockpit.ship` | `ShipV2` | ship `name` from ship.json (falls back to the first ship) |
| `cockpit.ambient` | 0.25 | ambient light 0..1 |
| `cockpit.light_intensity` | 1.0 | directional light strength |
| `cockpit.light_dir` | `0.35,0.75,0.55` | direction **toward** the light, view space (x right, y up, z back); stand-in until the world has a sun |
| `cockpit.glass_opacity` | 1.0 | multiplier on the canopy alpha (0 invisible, 1 as modelled, 3 heavy tint) |
| `cockpit.screen_brightness` | 1.0 | brightness of the content on the screens (0.2 - 2) |
| `cockpit.radar_range` | 400000 | radar rim distance in units (logarithmic scale) |

The light source lives in one function (`CockpitModule::light()` in cockpit.cpp). When the world module exists and provides the star direction through a service, that function
is the only place to change (rotate the world direction into view space).

## Testing the screens: `--fake-ship`
`ship/fake_ship` (not part of this module) provides a fake `ship::IShip` when started with
`--fake-ship=hp:35,shield:120,fuel:10,speed:42,hit:20,dead` (see its own docs). Without the flag the real `ship/ship_core` feeds the screens.
```
SDL_AUDIODRIVER=dummy ./space_game_v2 --frames=40 --screenshot=/tmp/cockpit.bmp --screenshot-frame=20 --fake-ship=hp:35,shield:120,fuel:10
echo '{"camera.mode": 1}' > /tmp/chase.json      # chase view: no cockpit;  ... --settings=/tmp/chase.json
./space_game_v2 --disable=ship/ship_core ...      # no IShip at all: screens say NO DATA (one warning)
```

## OBJ / MTL parser (`core/import_handler/obj_parser.h`)
`core::parseObj(text, mtlReader)` / `parseObjFile(path, out)`: no SDL/GL. Faces `v`, `v/vt`, `v//vn`, `v/vt/vn`, negative indices, quads and n-gons fan-triangulated, flat normals computed when a vertex has none,
per-vertex RGBA from the MTL (`Kd`, `d`/`Tr`; CANOPY forced to 30% alpha), a neutral grey when the MTL or material is missing, bad faces skipped with a warning (`ImportHandler` logs the warnings).
`core::Mesh` gained `colors` (rgba per vertex, empty for custom loaders) and `tagged`; `positions`/`normals` and `ImportHandler::load<Mesh>` are unchanged.

## Not done / later
* Radar shows bodies only (no asteroids/stations/ships yet); heading shows only `forward()` (no roll indicator).
* The light is a fixed direction until the world module supplies a sun direction.
* Screens are drawn in the pass, not to a texture: text is a stroke font, sharp at any distance but plain.
