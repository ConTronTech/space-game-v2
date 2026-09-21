# Cockpit (`ship/cockpit`)

Draws the inside of the ship: an OBJ model glued to the camera (view space), plus live content on the model's custom
`@` screens. In **cockpit view** (`core::ICamera::showsShip() == false`) it is drawn glued to the camera; in **chase view** the same model is drawn from outside in world space (see "Chase view").

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

## How it draws (performance)
The model (opaque hull + light state, and the glass) is compiled into display lists on first use. `ScreenCanvas` does not call OpenGL: every drawing call appends coloured quads to a `ScreenMesh`; the cockpit records each screen
into its mesh at `cockpit.screen_hz` and draws each screen with one `glDrawArrays(GL_QUADS)` (screens never write depth: contents are drawn in the order they were recorded). Text is still geometry, so it is crisp at any distance.
Consequence for renderers registered through `ICockpitScreens`: they are called at `cockpit.screen_hz`, not every frame, so they must be pure functions of the game state and `ScreenContext::time`. See docs/PERFORMANCE.md.

## What it does each frame
Render pass `ship/cockpit` (order 800, after the world). In this order:
1. skip if `cockpit.enabled` is false, the model did not load, or the camera shows the ship (chase view: the separate `ship/model_chase` pass draws instead);
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

The ship named by tunable `cockpit.ship` (default `ShipV3`; `ShipV2` and `ShipV1` stay selectable) is used; if there is no such ship the first one (sorted by folder name) is used
and a warning says so. The log line `[cockpit] ship 'X' (folder/model)` tells which was chosen. `shipv1` (plain, no screens) works.

## The `@` convention (custom materials)
**Any material whose name starts with `@` is not ordinary shaded geometry.** The parser pulls its faces out as a *tagged quad*
(`core::TaggedQuad`, in `core::Mesh::tagged`); they are **not** in the triangle list, so they are never drawn as coloured mesh. Something else draws
custom content on them.

Tag format `@GROUP-NAME`: GROUP is the text before the first `-`, NAME the rest.

| material | group | name | note |
|---|---|---|---|
| `@HUD-INFO` | `HUD` | `INFO` | ShipV2 / ShipV3 flight-data screen |
| `@HUD-SYSTEMS` | `HUD` | `SYSTEMS` | ShipV2 / ShipV3 ship-systems screen |
| `@HUD-RADAR` | `HUD` | `RADAR` | ShipV2 / ShipV3 radar screen |
| `@THRUST-JET` | `THRUST` | `JET` | engine exhaust origin (ShipV3), NOT a screen: see "Thruster jets" below |
| `@SCREEN-MAP-LEFT` | `SCREEN` | `MAP-LEFT` | only the first dash splits |
| `@GLOW` | `GLOW` | *(empty)* | no dash: whole text is the group |
| `NOTA@TAG` | – | – | `@` not first: an ordinary material |

A tagged quad carries `tag, group, name, corners[4], normal, centre, width, height` and `at(u, v)` (u 0..1 left->right, v 0..1 top->bottom).
Corners are as the modeller wrote them: bottom-left, bottom-right, top-right, top-left (counter-clockwise seen from the front).
A tagged face with more than four vertices uses the first four, a triangle is completed to a parallelogram; both add a warning. Bad faces are skipped, never a crash.
Every tagged face except the `THRUST` group becomes a screen.

## Thruster jets: `@THRUST-JET`
Paint the spot where the engine exhaust comes out with a material named `@THRUST-JET` (a flat disc or quad on the nozzle, facing backwards). Any shape works:
triangles, quads, n-gons, a triangle fan. Faces that share a vertex or lie within 0.5 m of each other are one **emitter**; two engines far apart are two
emitters (they share the exhaust particles). The faces are not drawn. Because the parser's tagged quads cannot tell a triangle from a parallelogram, the
jet faces are read straight from the OBJ text (`jetPolygonsFromObj`, `model_thrusters.h`, pure, tested in `tests/test_thrusters.cpp`).
Per emitter (`cockpit::ThrusterJet`, ship frame: eye at origin, +x right, +y up, -z forward, metres): `position` = area-weighted centre, `direction` =
the area-weighted face normal from the vertex winding (counter-clockwise seen from behind the ship), flipped if it points toward the hull's
bounding-box centre (the exhaust always leaves the body), `radius` = farthest jet vertex from the centre.
`cockpit::IShipModel::thrusters()` returns them (empty before the model loads or when the model has no tag: fx/particles then uses its old
`fx.nozzle_back` / `fx.nozzle_down` constants). They are logged once at load, e.g. ShipV3:
`[cockpit] thruster jet 0: pos (0.00, -0.38, 7.83) dir (0.00, 0.00, 1.00) radius 0.91` (the disc sits 0.9 m inside the nozzle, whose rim is at z = 8.72).
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
* **Stations** (`world::IStations`, optional): a cyan diamond with the same log range, rim clamp and height stems as bodies; **hollow** normally, **filled** when `ship::IDocking::nearestDockable` says docking works now for THAT station
  (the HUD's `DOCK [G]` state; other stations stay hollow). A second label line under the body label gives the nearest station and its distance from the surface: `STATION 1  96` (the "(Planet 1, orbital)" part of the name is dropped).
* **Asteroids** (`world::IAsteroids`, optional): the 12 nearest within `cockpit.radar_asteroid_range` (default 3000 units) as tiny dim tan dots (with the Ore Scanner perk: ore colours, rare ores one size bigger; `radarRockDot` in radar_map.h, docs/MINING.md), size by radius (under 2 / under 4 / larger), same plot as bodies, no stems, no labels, drawn under bodies and stations.
  The "nearest" query is O(rock count), so it is repeated 4 times a second, not every frame. Nothing is drawn when the module is absent or nothing is in range. The old demo rocks (`flight.demo_rocks`) are never on the radar.
* Everything on the scope is capped: 20 bodies + 6 stations + 12 asteroids (`kMaxRadarMarkers`), fixed arrays, no allocation in the plotting (the screen is cached at `cockpit.screen_hz` anyway).
Pure maths (frame, mapping, height offset/level test, nearest-N list, distance and height text) is in `radar_map.h`, unit-tested in `tests/test_cockpit.cpp`.

## Tunables (`config/game.json`)
| key | default | meaning |
|---|---|---|
| `cockpit.enabled` | true | draw the cockpit at all |
| `cockpit.ship` | `ShipV3` | ship `name` from ship.json (falls back to the first ship) |
| `cockpit.ambient` | 0.25 | ambient light 0..1 |
| `cockpit.light_intensity` | 1.0 | directional light strength |
| `cockpit.sun_light` | true | light the model with the REAL sun (`world::IStarSystem`); false = the fixed view-space `cockpit.light_dir` (the old look) |
| `cockpit.light_dir` | `0.35,0.75,0.55` | fallback direction **toward** the light, view space (x right, y up, z back), used without a star system or with `sun_light` false |
| `cockpit.chase_model` | true | draw the real ship model in chase view (false: the old wireframe fighter from ship_core) |
| `cockpit.glass_opacity` | 1.0 | multiplier on the canopy alpha (0 invisible, 1 as modelled, 3 heavy tint) |
| `cockpit.screen_brightness` | 1.0 | brightness of the content on the screens (0.2 - 2) |
| `cockpit.screen_hz` | 30 (by preset: low 15, medium 30, high 120, ultra 240) | how often the screens are redrawn per second; between redraws the cached geometry is drawn. At or above the frame rate = every frame; 0 = every frame |
| `cockpit.radar_range` | 400000 | radar rim distance in units (logarithmic scale) |
| `cockpit.radar_asteroid_range` | 3000 | asteroids closer than this are shown on the radar |

The light is decided in one function (`CockpitModule::computeLight`): the direction from the ship to the sun (`IStarSystem::sunPosition()` minus the ship position, in double) rotated into view space (`dirToViewSpace`), coloured with the sun's colour and scaled by `cockpit.light_intensity`.
It is set every frame outside the display lists, so it follows the sun and the ship's orientation; cockpit and chase view use the same sun, so the cockpit lighting matches the world.

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
* Radar shows bodies, stations and nearby asteroids (no ships yet); heading shows only `forward()` (no roll indicator).
* The light is a fixed direction until the world module supplies a sun direction.
* Screens are drawn in the pass, not to a texture: text is a stroke font, sharp at any distance but plain.

## Chase view (V)
* **Camera** (`core/camera`, `cam::chasePose`): a **rigid offset camera in the ship frame**. Its orientation is exactly the ship's (roll included) and it sits `camera.chase_distance` (27) behind and `camera.chase_height` (7) above the pilot's eye, measured along the ship's own forward and up, looking parallel to the ship's forward.
  The skybox, stars and planets are all drawn with the camera's view rotation, so the sky is identical to cockpit view (verified pixel for pixel with a rolled and pitched ship): pressing V only moves the eye.
  **What was wrong:** the old chase camera aimed at a point `0.6 x distance` ahead of the ship, which tilted the whole view down by `atan(3.5 / (12 x 1.6))` = about 10.3 degrees relative to the ship, so the sky (and every star and planet) was rotated by that pitch compared with cockpit view; nothing in the skybox or the passes was at fault.
  Defaults changed from 12 / 3.5 to 24 / 6 because the real model is 11.8 m long and reaches 8.7 m behind the eye: at 12 m the camera sat inside the hull's tail. With ShipV3 (13.6 m long, 10.1 m behind the eye, 1.38 m above it) they are 27 / 7.
* **Model** (`ship/model_chase` pass, order 110, `drawChasePass`): the ShipV2 display lists drawn in world space with `modelview = view x shipToWorld`, the columns of shipToWorld being (right, up, -forward, position) of the pose the camera uses (`ITransformSource::transform(alpha)`). The model is authored in view space (eye at the origin, +X right, +Y up, -Z forward, metres), so this puts the pilot's eye at the ship position and the nose forward.
  `chaseModelView` computes the matrix in double relative to the camera (the ship can be 40,000+ units from the origin). Depth test against the world; opaque hull first, then the see-through glass without depth writes (same lists as cockpit view, drawn once). The '@' screens face the pilot and are not visible from outside: skipped.
* **Wireframe fighter:** `ship/cockpit` provides `cockpit::IShipModel` (`ship_model_api.h`): `drawnInChase()` is true when the model is loaded and `cockpit.chase_model` is on; `ship_core`'s placeholder fighter is skipped then and stays as the fallback (no model, cockpit off).
* Model facts for other modules (logged at load as `[cockpit] hull x .. y .. z ..`). ShipV3: belly y = -1.38 (the same as ShipV2, so `docking.rest_height` stays 1.4),
  top y = +1.38, nose z = -3.51, rear tip z = +10.13, wingspan 11.5 m, 456 triangles (ShipV2: 178); the cockpit pass costs the same as with ShipV2
  (`--profile=gpu` on the dev PC: 0.33 vs 0.36 ms, CPU 0.035 ms both; one display list), so there is no LOD. ShipV2.obj: belly at y = -1.38 (a docked ship needs `docking.rest_height` about 1.4 to rest ON the pad; the default 0.6 sinks it 0.8 m in), nose at z = -3.1, rear tip at z = +8.74, thruster jets at z = +7.85, y = -0.38, wingspan 11.5 m.
