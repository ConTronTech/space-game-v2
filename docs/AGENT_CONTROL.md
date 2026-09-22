# Agent control (`core/input_methods/agent`)

A file-driven input device so an external controller - a script, another process, an AI agent - can actually fly
the ship: drive the exact same named actions a keyboard/joystick would, and read back a structured status snapshot
instead of screenshots. Not tied to any specific controller; it's a generic third "device" alongside keyboard and
joystick, following the same `core::InputMethod` extension point (`docs/INPUT.md`) - the input handler polls it
every frame regardless of which profile is loaded, so it needs no profile entry at all.

## Why a file protocol, not a socket/pipe

Simple, inspectable, and needs nothing beyond what every dev flag in this project already uses: plain files under
`logs/` (gitignored, worktree-local). A controller with shell access can drive the whole thing with `echo`/`cat`, no
client library.

## Protocol

Three files, all path-configurable (`agent.command_file` / `agent.control_file` / `agent.status_file`), default
under `logs/`:

- **`logs/agent_cmd.json`** - held input, read every frame: a flat JSON object of action name -> number, e.g.
  `{"thrust": 1.0, "yaw": -0.3, "fire": 1.0}`. Values are contributed **every frame** until the file's content
  changes, exactly like a real device continuously reporting what's currently held down - write `{}` to release
  everything. A missing or unparsable file contributes nothing (not an error). Any action name the game recognises
  works: `thrust`, `strafe`, `lift`, `pitch`, `yaw`, `roll`, `fire`, `weapon_1`/`weapon_2`/`weapon_3`, `dock`,
  `toggle_warp`, `toggle_orbit_lock`, `toggle_menu`, `lock_target`, `dump_debug_state`, ... (the full list is
  `config/input/default.json`'s action names, docs/QUICKSTART.md section 3).
- **`logs/agent_control.json`** - one-shot commands, consumed and deleted right after acting (never repeats).
  Currently just `{"craft": "<recipe id>"}`, which calls `gameplay::ICrafting::craft` directly - crafting is
  normally a menu click, not a flight input, so this skips simulating menu navigation for the one action that
  genuinely doesn't need it. The status snapshot has no dedicated field for the result: check `logs/game.log`
  for the `[agent] craft '...': OK|refused (reason)` line, or just check `cargo.perks` / `cargo.stacks` in the
  next status snapshot for the actual effect.
- **`logs/agent_status.json`** - overwritten `agent.status_hz` times a second (default 4): ship (hp/shield/fuel/
  speed/alive/warping/position/velocity), docking (docked, current station, nearest dockable + why not), cargo
  (used/capacity, every ore stack, perks owned - `ore_scanner`, `anomaly_scanner`), and the 12 nearest asteroids
  (id, ore type, distance, radius, position) as a sensor readout so the controller can navigate toward a specific
  ore without needing to read pixels.

## Example: fly forward, then check what happened

```sh
./space_game_v2 --display=1 --input-profile=testing &   # any profile works: agent needs no profile entry
echo '{"thrust": 1.0}' > logs/agent_cmd.json
sleep 2
python3 -c "import json; print(json.load(open('logs/agent_status.json'))['ship']['speed'])"
echo '{}' > logs/agent_cmd.json   # release
```

## Design notes

- Pure logic (command parsing, status JSON assembly) is in `agent_rules.h`, unit-tested in
  `package/tests/test_agent_input.cpp` - the module itself (`agent.cpp`) only does file I/O and service calls,
  gathering plain data and handing it to `agent::buildStatus`.
- Every service it reads (`ship::IShip`, `ship::IDocking`, `gameplay::IInventory`, `gameplay::ICrafting`,
  `world::IAsteroids`) is optional: a section is simply omitted from the status JSON if that module isn't loaded,
  matching the "absent optional service = no restriction/no crash" convention used everywhere else in this codebase.
- `priority()` is 200 (late) so every service it might read has already loaded by the time it inits.
