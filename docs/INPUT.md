# Input

Game code never reads keys, mouse or sticks. It asks for **actions**; a JSON **profile** in
`config/input/` says which physical inputs drive each action.

```
config/input/default.json     keyboard to move, mouse to look
package/modules/core/input_handler/            actions, profile loading
package/modules/core/input_methods/keyboard/   "device": "keyboard"
package/modules/core/input_methods/mouse/      "device": "mouse"
```

## In game code
```cpp
auto& in = eng.services.require<core::InputHandler>();
float thrust = in.value("thrust");   // -1..1
in.down("brake"); in.pressed("quit"); in.released("fire");   // "down" = |value| > 0.5
```
An action's value is the **sum of all its bindings**, clamped to -1..1. So "W", a stick axis and a
mouse axis can all drive `thrust` at once. Actions not in the profile just read 0.

**Rule:** use `pressed()` / `released()` in `onUpdate` (once per frame), never in `onFixedUpdate`, where several
physics steps can run in one frame and the same press would count more than once. `value()` and `down()` are
safe in both.

## Joysticks, wheels, pedals, shifters
A third device type, `core/input_methods/joystick` (docs/CONTROLLERS.md): raw SDL joystick axes / buttons / hats, one profile per device in `config/input/devices/`, values ADDED to the keyboard's and the mouse's (the same action names, through `contribute`). A wheel base with pedals and a shifter is ONE device with many controls. Nothing to bind in the keyboard profile.

## Ship actions in the default profile
`thrust strafe lift pitch yaw roll brake`, `toggle_warp` (Z), `toggle_orbit_lock` (O), **`fire` (left mouse), `weapon_1` / `weapon_2` (keys 1 / 2), `weapon_next` (mouse wheel)** (docs/COMBAT.md), **`toggle_menu` (I): the game menu (docs/GAME_MENU.md)**, **`dock` (G)**: docks at / undocks from the nearest station (docs/STATIONS.md), `camera_next` (V), **`toggle_profiler` (F3), `toggle_profiler_gpu` (F4), `dump_profile` (F5)**: the live profiler (docs/PERFORMANCE.md), `mouse_capture_toggle` (Tab), `pause` (Esc). The full list with keys is in docs/QUICKSTART.md.

## Profile format
```json
{
  "name": "Keyboard + Mouse",
  "devices":  { "mouse": { "capture": true, "sensitivity": 1.0 } },
  "bindings": {
    "thrust": [ { "device": "keyboard", "key": "W", "scale": 1 },
                { "device": "keyboard", "key": "S", "scale": -1 } ],
    "yaw":    [ { "device": "mouse", "axis": "x", "scale": -0.0006 } ]
  }
}
```
`//` comments are allowed. `scale` defaults to 1 (negative flips direction).

| device | binding fields |
|---|---|
| `keyboard` | `key`: SDL scancode name (`"W"`, `"Space"`, `"Left Shift"`, `"Up"`, `"Escape"`) |
| `mouse` | `axis`: `x` / `y` / `wheel` (pixels-per-second x scale, like a stick), or `button`: `left` / `middle` / `right` / `x1` / `x2` |

Mouse look needs `"capture": true`; bind an action named `mouse_capture_toggle` (default: Tab) to free the cursor.

Choose a profile: `./space_game_v2 --input-profile=<name>` (loads `config/input/<name>.json`).
Bad keys, unknown devices and broken JSON print a `[input]` message and never crash the game.

## Add a new device (e.g. joystick)
1. `package/tools/new_module.sh core/input_methods joystick`
2. Make the class also inherit `core::InputMethod` (`input_method.h`), depend on `core/input_handler`,
   call `registerMethod(this)` in `init` and `unregisterMethod(this)` in `shutdown`.
3. Implement:
   - `device()` - the string used in JSON (`"joystick"`)
   - `addBinding(action, json, err)` - parse your fields (e.g. `axis`, `button`, `deadzone`, `invert`)
   - `poll(in)` - read the device, call `in.contribute(action, value)` for each active binding
   - `clearBindings()`, optionally `configure(json)` for the profile's `"devices"` section
4. Use it in a profile: `{ "device": "joystick", "axis": 1, "deadzone": 0.15, "scale": -1 }`

`keyboard.cpp` is the smallest example. Nothing in the input handler or the game modules changes.
