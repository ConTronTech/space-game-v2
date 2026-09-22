# Space Game V2

A hard, strategic 3D space flight game: real Newtonian movement (no drag, no speed cap, no assists), single-body
sphere-of-influence gravity, mining, crafting, and exploration. It is not meant to be an easy game or a dopamine
reward loop - it's meant to be unforgiving and worth mastering. Space is unforgiving for everyone: there are no NPCs
yet, but when they arrive they'll follow the exact same rules as the player (same physics, same limits).

Built on a small C++23 module engine: every feature (input, rendering, UI, gameplay) is a self-contained, deletable
module discovered and started by a tiny kernel. Runs on plain OpenGL 2.1 fixed-function rendering, on purpose - no
shader dependency, so it stays usable on genuinely old/weak hardware. Linux only for now.

## What's playable right now

Newtonian flight with real gravity, a stable circular-orbit guide, warp drive, a seeded star system (sun, planets
with terrain LOD, moons, a Fresnel-glow atmosphere), asteroid belts with tiered ore by distance from the sun, mining,
crafting (gated behind blueprints found by exploring anomalies), orbital and planetary stations with docking, combat
(blaster, mining beam, homing missiles), a system map, radar, an in-game live profiler and state debugger, and
joystick/wheel/pedal support with a guided setup screen. See `docs/DEVLOG.md` for the full, newest-first history of
what was built and verified, and `docs/RESUME.md` for the current state and open items.

## Building

Needs SDL2, SDL2_ttf, SDL2_image, SDL2_mixer and OpenGL.

```sh
sudo package/tools/install_deps.sh --dev   # Debian/Ubuntu/Mint: compilers + dev libraries
make                                       # build ./space_game_v2
make run                                   # build and start the game
make test                                  # unit tests (no window/SDL needed)
make test-san                              # same tests under AddressSanitizer + UBSan
```

No `sudo`? Use `package/tools/bundle_libs.sh` instead to ship the runtime libraries alongside the binary rather than
installing them system-wide.

Full build details, every command-line flag, and the full control scheme: `docs/QUICKSTART.md`.

## Project layout

- `package/engine/` - the kernel: module discovery, the fixed-timestep game loop, config, events, services.
- `package/modules/` - every feature, one self-contained folder each (e.g. `ship/gravity`, `world/star_system`,
  `ui/game_menu`). A module can be deleted without breaking the rest of the game.
- `package/tests/` - the unit test suite (`make test`).
- `assets/`, `data/` - models, skybox art, and the JSON tables that drive ores, recipes, weapons, blueprints, etc.
- `docs/` - design intent (`VISION.md`), the full dev history (`DEVLOG.md`), and one reference doc per system.

## Docs worth reading first

- `docs/QUICKSTART.md` - build, run, every flag, every control.
- `docs/VISION.md` - what this game is trying to be, in the author's own words.
- `docs/MODULES.md` - how the module system works, if you want to add or change something.
- `docs/DEVLOG.md` - the full, newest-first history of every verified change.

## On the development process

Large parts of this codebase were built with AI coding assistance (Claude, via Claude Code) working from specs and
under review by the author, alongside hand-written fixes for foundational/architectural pieces. `docs/DEVLOG.md`
documents this plainly, leap by leap. The design, direction, and every decision are the author's own.

## License

GNU General Public License v3.0 - see `LICENSE`. Free to use, study, modify, and share; any derivative work must
stay open source under the same license. This project is not for sale, and no closed-source fork of it may be sold
either - that's the point of the GPL.
