# Next up - planning notes (2026-09-22 chat)

**Status: all 10 items below are BUILT** (overnight session, 2026-09-22, tag `good-20260922-26`). Each item's
commit is noted inline. Open questions/judgement calls from the build are logged in `docs/QUESTIONS.md` #15-20 -
review those first, they're the practical follow-ups (balance numbers, a few things that were never visually
checked since they were built by agents that couldn't launch the game).

Decisions from a planning conversation, not yet built. Written down so worker agents have a clear spec instead of
re-deriving it. Each section names what to change, why, and what's already true in the code today.

## 1. Atmosphere shell size
`world/atmosphere/atmosphere_rules.h`'s `AtmosphereParams::shellFactor` is `1.06` (shell radius = 1.06x body
radius). Change to **1.4** (40% above the planet body). One-line tunable, no other logic changes needed.

## 2. Planet/moon surface: keep procedural LOD, add texture wrapping for color/material variety
Decision: do **both**, split by role, not either/or.
- Surface **shape** (mountains, craters) stays the existing procedural LOD mesh (`world/star_system/planet_mesh.h`)
  generating real geometry - that is the actual depth, and it is the only depth technique that works cleanly under
  this engine's constraint of fixed-function OpenGL 2.1, no shaders (`docs/VISION.md`).
- Add **diffuse texture wrapping** on top for color/material variety per body (rock, ice, sand, gas-giant bands,
  moon regolith vs planet biomes) - a flat UV-wrapped color texture is cheap and fully supported in fixed-function
  GL.
- Explicitly **not** doing shader-style normal/bump mapping (dot3 combiner tricks are old-GPU-unfriendly and
  fight the compatibility target) - "depth" from geometry, "variety" from texture, kept separate.
- Textures are **pregenerated at load time**, not decoded/generated during flight: bake to CPU pixel buffers and
  upload once as part of the boot sequence (`core/boot_screen`'s per-module `onBootOverlay` replay is the natural
  place for a progress step), same reasoning as the skybox-color fix in Leap ~53 (avoid a runtime hitch the first
  time a planet comes into range).
- Open item before implementation: survey what fixed-function multitexture options exist (e.g. blending two
  textures by a mask - snow cap vs equator band) so the "variety" system has a concrete technique, not just "add a
  jpg."

## 3. Main menu before gameplay
Today there is no main menu at all - `core/boot_screen` runs, then the game drops straight into a freshly
generated world every launch (nothing calls `loadSlot`/`newSlotName` at startup; those only exist inside
`ui/pause_menu`). Add a new `ui/main_menu` module between boot screen and gameplay, with options: **New Game,
Continue (last save), Load, Settings, Quit**. This needs `engine::Engine` to support a "no live session yet" phase
- init currently assumes a live world exists.

## 4. Save system rework
`core/save_system` (`save_system.h`/`.cpp`) already has: named slots (`validSlotName` allows letters/digits/`_`/`-`),
atomic writes, `listSlots()`, format-safe load. What's missing, to build:
- **Named saves via UI** - a text-entry "Save As" field (backend already supports arbitrary valid names, this is
  a UI gap only).
- **Autosave** - a reserved, always-overwritten slot (e.g. `autosave`), triggered on an interval or at key moments
  (dock, warp exit) - cadence/trigger policy still needs deciding.
- **Active-slot / last-save tracking** - **lives in `core/save_system` itself** (explicit user decision - it's
  save-session state, not menu-UI state, so both the pause menu and the new main menu can query it: "what's the
  active slot," "when did we last save," "what should Continue load").
- **Save vs Save As** - "Save" overwrites the currently active/loaded slot; "Save As" prompts a new name. Today
  the pause menu's Save button always calls `newSlotName()` (always creates a new timestamped slot) - this needs
  the active-slot tracking from above to know when to overwrite vs create.

## 5. Asteroid rings and cluster distribution
Two problems found in `world/asteroids/asteroid_rules.h`'s `generateField`:
- **Clusters starve most planets**: `clusterCount` defaults to 4, and the cluster loop shuffles over *all*
  non-sun bodies (planets AND moons, `asteroids.cpp:65-68`) with equal weight. A system with several moons means
  most of those 4 slots go to moons, leaving most planets with nothing nearby - this is why only one planet has
  ever had a findable cluster in testing.
- **No true rings exist**: the current "cluster" is a spherical shell around a body (random direction, flattened
  by half in Y) - it reads as loose asteroids near a moon, not a flat orbital ring hugging a planet's equator.

Decisions:
- Split planet and moon target pools for placement so moons can't crowd planets out of getting *something* nearby
  (exact guarantee/budget split still to be designed).
- Add a genuine **ring generator**: same shape as the existing belt code (angle + radius band + thin height) but
  centered on a planet instead of the sun, giving a flat, orbit-like band instead of a puffy shell.
- Rings are a **seeded per-planet chance, not guaranteed on every planet** - some planets ringed, some bare, like
  real systems. Not every planet.
- Keep the existing shell-cluster type for moons/loose-asteroid flavor; it just stops competing 1:1 with planet
  rings for the same slot budget.

## 6. Asteroid belt count and placement bias
`GenParams::beltCount` is currently a fixed config value (effectively always 1 belt). Change to:
- **Random belt count per system, 1-5** (seeded).
- **Outward placement bias**: belts skew toward existing farther out from the sun - a system that rolls only 1
  belt is likely to get it in one of the farther orbital gaps; higher rolls (3-5) fill the inner gaps too. This
  mirrors the existing ore-zone rule (rarer/valuable ore already skews outer, `data/ore_zones.json`).
- Today's gap selection is a uniform seeded shuffle over all eligible gaps (`generateField`'s belt loop,
  `asteroid_rules.h` lines ~140-144) - this needs to become a distance-weighted pick (outer gaps picked
  first/more often) instead of pure shuffle.
- **Explicitly not** changing belt width or asteroid count per belt - placement bias only, everything else about
  an individual belt stays as-is.

## 7. Inventory rework: unified slot grid
Today's model (`gameplay/inventory`, see docs/INVENTORY.md "Cargo rework: per-resource holds + general hold") is
pool-based: every ore gets its own numeric pool capped by `cargo_cap`, crafted items share a separate 30-unit
general pool by volume, and there is no visual slot representation - it is all just numbers. Replace with a
grid-based inventory, similar to the original game:

- **Unified grid**, fixed size (example: 8x12 = 96 slots) - ore and crafted items share the same grid, no more
  separate ore-pool/general-pool split. Grid slot COUNT does not change with cargo upgrade level.
- **Per-item stack cap scales with cargo upgrade level** instead of slot count growing - each ore/item's existing
  base cap (from `data/ores.json` / `data/items.json`) is the level-1 stack size, and it multiplies up per cargo
  level (example: uranium's stack cap 10 at level 1 scaling toward ~60 near the top level, ~20, with total
  cargo-wide capacity reaching roughly 2000 units at max level). Same idea as today's `resource_mult`/`general_mult`
  in `data/cargo.json`, just applied as a per-stack multiplier instead of a pool-total multiplier.
- **Auto-stack on pickup**: mining/crafting fills an existing same-id slot first (up to its stack cap), then spills
  into a new empty slot; a full grid with no matching or empty slot behaves like today's `CargoFull` refusal.
- **Drag-to-trash discard**: dragging a slot onto a trash icon discards it (replaces/extends the CARGO tab's
  discard-button softlock fix from 5.1c). Needs a controller/non-mouse equivalent (select + discard button) for
  wheel/joystick-only play (docs/DEVICES.md, docs/CONTROLLERS.md) - this can't be mouse-drag-only.
- **Side info panel** next to the grid: tooltips and general item/ore info on selection/hover.
- Touches: `gameplay/inventory` backend (slots array instead of id->amount stacks + a generalUsed pool), the CARGO
  tab UI (`ui/game_menu` or wherever it lives), mining pickup logic, crafting's ingredient check (now counts across
  potentially multiple same-id slots), and the save format (`gameplay/inventory`'s `stacks` list needs a slot index
  or auto-relayout-on-load, still to be decided).

## 8. Game menu tab keys collide with strafe
`docs/QUESTIONS.md` #12: the `I` menu's tab-switching uses the `ui_left`/`ui_right` actions, which are the same
bindings as A/D strafe. Switching tabs with A/D while the menu is open also strafes the ship (the menu does not
pause the game by default, `menu.pause_game` is off). Fix: give the menu its own dedicated tab-switch keys (`[`
and `]` suggested) instead of reusing `ui_left`/`ui_right`.

## 9. Dock key HUD label is hardcoded
`docs/QUESTIONS.md` #7: the HUD's dock prompt always shows "G" regardless of what is actually bound, because
`core::IInput` does not expose a way to look up a binding's key back out. If dock is ever rebound, the prompt
lies. Fix needs a small addition to `IInput` (or wherever bindings are queried) so the HUD can read the real
bound key instead of a literal string.

## 10. Quick Action Bar (No Man's Sky-style radial/bar quick menu)
Ported concept from the old game (`include/ui/quickbar/quick_action_bar.h`) - has no equivalent in V2 today, and
directly addresses the "no spare buttons" problem on the retro rig (wheel + joystick, limited inputs): a D-pad
flight overlay for instant actions, opened without pausing or leaving flight, instead of the full `I` menu.
- **Open/close**: hold D-pad up (or equivalent bound action) to open; D-pad down or a timeout auto-closes it
  (old game: 5 s idle timeout).
- **Does not pause the game** - a fast in-flight action picker, not a menu screen.
- **Two action kinds**: instant (tap to fire immediately, e.g. use Ore Scanner) and toggle (tap to flip on/off,
  shows ON/OFF state, e.g. orbit lock, shield enable). Each action has an `available()` check (greyed out when
  not usable) and an optional per-action cooldown with a visual fill-down overlay.
- **Navigation**: D-pad/stick left-right scrolls the selection, confirm (A / bound action) activates the selected
  one; wraps around at the ends.
- Bar contents should be data-driven (what actions are available depends on what's unlocked - perks, crafted
  items, ship upgrades), not a hardcoded list.

**Researched against real No Man's Sky behavior (its PC Quick Menu is `X`, per-slot bindable via Ctrl+number; scan
`C` and weapon-mode swap `G` are separate dedicated keys, not in the radial - the radial there is mainly for
consumable/technology toggles). Decision: do both, not either/or -**
- **Weapon select gets BOTH a dedicated key (existing 1/2/3 + mouse-wheel `weapon_next`) AND an entry in the quick
  bar** - keyboard players keep the fast direct keys, but it's also reachable through the bar (useful for
  wheel/joystick, which has no number-row equivalent, and as a visual "what's selected" reference either way).
- **Toggles**: orbit lock, warp, shield enable/disable.
- **Instant-use perk/item actions**: Ore Scanner activate, Anomaly Scanner activate, and (later) beacon/distress-
  signal scan once that Phase 6 item (#5, distress beacons) exists.
- Explicitly **not** duplicating fire/thrust/steer/dock - those stay on their own always-live dedicated controls
  regardless of device; the bar is for secondary/situational actions, not primary flight input.

- Touches: a new UI module (e.g. `ui/quick_action_bar`), input bindings (`config/input/default.json` needs an
  open/confirm/scroll set that doesn't collide with flight controls), and whatever service each action calls into
  (docking, orbit lock, perks/items, weapon select, etc. - all already exposed as services, this is just a new
  front door to them).

## Also flagged, not yet folded in (revisit after #5/#6 land)
- **Belt density is sparse by default** (`docs/QUESTIONS.md` #5: ~9 rocks in the densest 1500-unit patch of the
  single default belt). More belts existing (item 6 above) does not by itself fix a thin belt - worth reconsidering
  `asteroids.belt_asteroids` / patchiness once the belt-count rework is in and testable.

## Status
All ten items built overnight 2026-09-22 (tag `good-20260922-26`). Next up: the Phase 6 game loop list resumes at
`gameplay/solar_flares` (`docs/GAME_LOOPS.md` item 4), once the open questions in `docs/QUESTIONS.md` #15-20 are
reviewed.
