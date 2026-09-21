# Space Game V2 - Quick Start

## 1. What this is

Space Game V2 is a 3D space flight game written in C++23, built on a module-based
engine: every feature (input, rendering, UI, assets, gameplay) is a module under
`package/modules/`, discovered and started by the tiny kernel in `package/engine/`.

## 2. Build and run

The system needs SDL2, SDL2_ttf, SDL2_image, SDL2_mixer and OpenGL.

```sh
make            # build ./space_game_v2
make run        # build and start the game
make test       # build and run unit tests (no window/SDL needed)
make test-san   # same tests under AddressSanitizer + UBSan
make modules    # list loaded modules (== ./space_game_v2 --list-modules)
./space_game_v2 # run the already-built binary
```

`package/tools/smoke.sh` does a clean strict `-Werror` build, runs the unit tests
plain and under sanitizers, checks the module count and does a short 60-frame
game run with a screenshot - use it before each commit.

In a fresh worktree, link the git-ignored assets folder first:
`package/tools/link_assets.sh`.

## 3. Controls

Bindings from `config/input/default.json` (profile "Keyboard + Mouse").

| Action | Keys / device |
|---|---|
| thrust | W (forward), S (reverse) |
| strafe | D (right), A (left) |
| lift | Space / R (up), C / F (down) |
| roll | E (right), Q (left) |
| yaw | mouse x-axis, Left / Right |
| pitch | mouse y-axis, Down / Up |
| brake | X or right mouse button |
| camera_next | V (cockpit <-> chase view) |
| mouse_capture_toggle | Tab (free/capture cursor) |
| pause | Escape |
| ui_up | Up or W |
| ui_down | Down or S |
| ui_left | Left or A |
| ui_right | Right or D |

Esc opens the pause menu (Resume, Save Game, Load Game, Settings, Exit Game).

## 4. Useful flags

```sh
./space_game_v2 --paused                 # start with the pause menu open
./space_game_v2 --paused=settings        # start in the settings page
./space_game_v2 --paused=load            # start on the load-save page
./space_game_v2 --screenshot=file.bmp    # save a screenshot at frame 30, game continues
./space_game_v2 --frames=N               # exit after N frames (0 = unlimited)
./space_game_v2 --disable=category/name # skip a module at runtime (comma-separated list)
./space_game_v2 --input-profile=name     # load config/input/<name>.json instead of default
./space_game_v2 --settings=<file>      # use another player-preferences file
./space_game_v2 --saves=<dir>          # save to another directory
./space_game_v2 --data=<dir>           # load content from another directory
./space_game_v2 --list-modules           # print loaded modules and exit
```

`--disable` takes module names like `ship/cockpit,core/ui_handler`.
To disable a module at build time instead, prefix its folder with `_`.

## 5. Where things live

| Path | What |
|---|---|
| `config/` | `game.json` hand-edited tunables (generated on first run; values stay "DEFAULT" until you change them), `settings.json` player preferences written by the pause menu, `input/*.json` input profiles, `ui/theme.json` the glass theme |
| `data/` | game content as JSON - `ores.json`, `items.json`, `recipes.json`, loaded by the data registry |
| `saves/` | save slots written by the save system |
| `assets/` | models and skybox images (git-ignored; linked in by `package/tools/link_assets.sh`); sounds are synthesized in code, optional files go in `assets/sounds/` |
| `package/engine/` | the kernel: module interface, event bus, services, main loop |
| `package/modules/` | all modules: `core/` (window, input, rendering, UI, saves, settings, audio, data), `ship/` (ship core, cockpit, dev fake ship), `ui/` (pause menu, ship HUD) |

For more: `docs/MODULES.md` (modules), `docs/INPUT.md` (input profiles),
`docs/WORKFLOW.md` (how phases are built).
