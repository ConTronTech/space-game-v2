# Graphics quality (`core/quality`)

The game picks a graphics preset for the machine at startup; the player can override it. A preset is a set of **default values for existing tunables**
(`engine.config`, see docs/CONFIG.md). Nothing else changes: the modules just call `config.get` as before.

**Precedence for every tunable:** your value in `config/game.json`  >  the preset's value  >  the default in code. (`"DEFAULT"` in game.json means "not overridden", so the preset applies.)

## Which preset
1. `--quality=low|medium|high|ultra|auto` (dev flag), else
2. the pause menu's **Settings > Graphics** row (saved as `quality.preset` in `config/settings.json`; applies on the **next launch**, the label says "(restart)"), else
3. `quality.preset` in `config/game.json`, else **auto**.

Auto decides from the hardware facts `core/window` also logs (GL renderer/vendor string, max texture size, CPU threads, RAM, desktop size). The log at startup shows the facts, what auto would pick and why, the preset in use, and every value applied:
```
[quality] hardware: NVIDIA GeForce RTX 2060 SUPER/PCIe/SSE2 | NVIDIA Corporation | max texture 32768 | 20 CPU threads | 40026 MB RAM | display 1920x1080
[quality] auto would pick 'ultra' (NVIDIA RTX)
[quality] preset in use: ultra (auto) (chosen by game.json or default); game.json values still override it
```

### Auto heuristic (`quality_rules.h`, unit-tested with sample strings)
| Renderer | Preset |
|---|---|
| software (llvmpipe, softpipe, swrast, SwiftShader, GDI Generic) | Low |
| Intel Ironlake / Sandy Bridge / HD Graphics / older (the design target, docs/VISION.md) | Low |
| Intel Iris / UHD / Xe | Medium |
| Intel Arc | High |
| NVIDIA RTX | Ultra |
| NVIDIA GTX / Quadro / Titan / other modern | High |
| older NVIDIA (GeForce GT, 8xxx-9xxx) | Medium |
| AMD Radeon RX 5000+ (4-digit) | Ultra |
| AMD Radeon RX (400/500 series) / Pro / Navi | High |
| AMD integrated (Radeon Graphics, Vega) / older Radeon | Medium |
| Apple | High |
| unknown or empty | Medium |

Limits that only ever lower the result: max texture < 4096 -> Low; <= 2 CPU threads or < 3 GB RAM -> at most Medium; a 4K+ desktop -> Ultra becomes High.

## What each preset sets
Medium equals the built-in defaults, so an unknown machine plays exactly as before.

| Tunable | Low | Medium | High | Ultra |
|---|---|---|---|---|
| `skybox.max_size` (px) | 512 | 1024 | 2048 | 2048 |
| `starfield.count` | 1200 | 2500 | 4000 | 6000 |
| `world.sphere_detail` | 0 | 1 | 1 | 1 |
| `world.planet_max_lod` | 2 | 3 | 3 | 4 |
| `world.planet_triangle_budget` | 8000 | 20000 | 40000 | 80000 |
| `world.planet_lod_edge_px` (smaller = finer) | 20 | 12 | 8 | 6 |
| `asteroids.belt_asteroids` | 600 | 1500 | 2500 | 4000 |
| `asteroids.cluster_asteroids` | 30 | 60 | 100 | 150 |
| `asteroids.max_drawn` | 150 | 400 | 700 | 1200 |
| `asteroids.draw_distance` | 3500 | 6000 | 8000 | 10000 |
| `asteroids.triangle_budget` | 6000 | 15000 | 30000 | 60000 |
| `asteroids.lod_edge_px` | 16 | 10 | 8 | 6 |
| `render.scale` | 0.7 | 0.85 | 1.0 | 1.0 |
| `render.clear_color` | false | true | true | true |
| `starfield.points` | false | true | true | true |
| `cockpit.glass_tint` | false | true | true | true |
| `cockpit.screen_hz` | 15 | 30 | 120 | 240 |

(The skybox source faces are 1024 px today, so High/Ultra only pay off with larger face images.) Not tied to a preset: vsync (the window sets it; `--no-vsync` for benchmarks), `world.terrain_height`, gameplay tunables.
To add a tunable, add a row to `presetTable()` in `quality_rules.h` (the table test checks monotonic growth).

## How it is wired (ordering)
`engine::Config` has a small **preset layer** (`setPresetNumber/Bool/String`) consulted by `get()` between game.json and the code default. `core/quality` fills it in `init`.
Init order: settings (-1500) < window (-1000, creates the GL context whose strings we read) < **quality (-900)** < import_handler (-200) < render_engine (-100) < the world modules (they depend on render_engine). Any module that reads a quality-scaled tunable must init after quality (priority > -900).

## Service and safety net
`core::IQuality` (`quality_api.h`): `selectedName()`, `activeName()`, `detectedName()`, `reason()`. The benchmark prints `quality preset:` and the pause menu shows `Graphics: Auto (Ultra)`.
With Auto, if the average over the first ~10 s of play is under 25 fps the log warns to try a lower preset; it never changes settings by itself.

Round 2 rows (`render.scale`, `render.clear_color`, `starfield.points`, `cockpit.glass_tint`) are described in docs/PERFORMANCE.md; the on/off ones are 0/1 switches (only Low changes).
