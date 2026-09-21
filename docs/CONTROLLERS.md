# Controllers: joysticks, wheels, pedals, shifters (`core/input_methods/joystick`)

A third input method next to the keyboard and the mouse. It uses SDL's **raw joystick API** (not the game-controller database: racing wheels and flight sticks are not in it), so every axis, button and hat is addressed by number.
Code: `package/modules/core/input_methods/joystick/` (`joystick_rules.h` and `joystick_profile.h` are pure and unit-tested with simulated devices by `package/tests/test_joystick.cpp`).

## How it works
* **One device, many controls.** A steering wheel base is ONE joystick device exporting steering, the pedal axes (throttle / brake / clutch, possibly combined on one axis), the stick shifter's buttons, wheel buttons and a hat. Everything is `device + axis N / button N / hat N`; pedals and a shifter plugged in later are just more axes and buttons of the same device.
* **Profiles** (`config/input/devices/<vid>_<pid>_<name>.json`) say what each control does. A device gets its profile automatically: exact USB vendor+product match first, then a name substring, else the **generic fallback, which maps nothing** (so an unknown device can never make the ship spin) and logs "run --list-joysticks and --joystick-calibrate".
* **Values add.** The result of each control is contributed to a game action (`roll`, `pitch`, `yaw`, `thrust`, `fire`, `dock`, `toggle_warp`... the same names as `config/input/default.json`) through `IInput::contribute`, exactly like the mouse: the stick and the keyboard work at the same time and their values add (clamped to -1..1).
* Hot-plug works (`SDL_JOYDEVICEADDED/REMOVED`); it keeps working when the window is unfocused (`SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS`). A bad profile file is logged and skipped (that device falls back); nothing crashes.
* At startup a log block lists every device: name, `vid:pid`, axes / buttons / hats, GUID and the matched profile.

## Axis processing (in this order)
1. **Calibrate** raw int16 to -1..1 around the calibrated centre (`min`, `max`, `centre`: each half is scaled to its own stop, so an off-centre stick still reaches +-1).
2. **Dead zone** with rescaling: the output is 0 inside it and starts from 0 right after it (continuous), still reaching 1.
3. **Curve**: `sign * |v|^curve` (1 = linear, 1.5 = gentle around the centre, 0.7 = twitchy).
4. **Invert** and **scale**.
Pedals: `role` `throttle` / `brake` / `clutch` are 0..1 with `rest` = the end the unpressed pedal rests at (`min` = -32768, `max` = +32767): an unpressed pedal reads 0, fully pressed 1. `combined_pedals` (one axis for two pedals): above the centre = the `above` action (default `thrust`), below = the `below` action (default `brake`), each 0..1.
User tuning (settings, applied live): `input.joystick_enabled` (true), `input.joystick_deadzone_scale` (0.5..2, multiplies every dead zone), `input.joystick_sensitivity` (0.1..4, multiplies stick / wheel axes, not pedals). The pause menu does not have rows for them yet (see the end).

## Profile format (annotated)
```jsonc
{
  "name": "PXN V10 Wheel",
  "status": "UNVERIFIED: run --joystick-calibrate",            // shown in docs / logs; the shipped profiles are all UNVERIFIED
  "match": { "vid": "11ff", "pid": "3245", "name_contains": "PXN" },   // hex ids as strings; either or both

  "axes": [
    { "index": 0, "role": "steering", "action": "yaw",          // roles: stick | steering | throttle | brake | clutch | combined_pedals
      "invert": true, "scale": 1.0, "deadzone": 0.04, "curve": 1.5,
      "calibration": { "min": -32768, "max": 32767, "centre": 0 } },
    { "index": 2, "role": "throttle", "action": "thrust", "rest": "max", "deadzone": 0.02 },
    { "index": 3, "role": "combined_pedals", "above": "thrust", "below": "brake" }
  ],
  "buttons": [
    { "index": 4, "action": "toggle_warp" },                    // mode "hold" (default, also "press"): active while pressed, like a key
    { "index": 9, "action": "toggle_orbit_lock", "mode": "toggle" }   // "toggle": each press flips it on / off
  ],
  "hats": [ { "index": 0, "up": "weapon_1", "down": "weapon_2", "left": "camera_next", "right": "dock" } ],
  "groups": [                                                    // several buttons -> one discrete value (the shifter's gears): the highest pressed button wins
    { "action": "throttle_limit", "buttons": { "12": 0.15, "13": 0.3, "14": 0.5 }, "default": 1.0 }
  ]
}
```
`//` comments are allowed in the files. The action names must exist in the game to have an effect (`throttle_limit` above is only an example: nothing reads it yet). Sign conventions (from `config/input/default.json`): `roll` + = roll right, `pitch` + = nose UP, `yaw` + = turn LEFT, `thrust` + = forward, `lift` + = up. SDL reports a stick pushed right, pulled back or twisted clockwise as positive.

## Shipped profiles (UNVERIFIED first guesses)
Microsoft SideWinder (`045e:003c`) and PXN V10 wheel (`11ff:3245`): tables in docs/DEVICES.md. They are meant to be corrected on the real hardware with the two tools below.

## Finding out which control is which
These flags need no window (only SDL's joystick subsystem), so they work over SSH:
| Command | What it does |
|---|---|
| `./space_game_v2 --list-joysticks` (`=json`) | table of devices: index, name, `vid:pid`, axes / buttons / hats, GUID and which profile matched; then exits |
| `./space_game_v2 --joystick-monitor` | 30 s of live changes: `#0 axis 2 = -32768 (-1.00) -> thrust`, `#0 button 4 DOWN -> toggle_warp`, hat changes; move ONE control at a time to see its number and what the profile does with it |
| `./space_game_v2 --joystick-calibrate` | 10 s: move EVERYTHING to both ends (wheel lock to lock, each pedal fully, shifter, stick). Prints min / max / start value per axis with a role guess ("pedal resting at max", "stick / steering (centred)", "unused") and writes a suggested snippet to `logs/joystick_calibration.json` |
Workflow: run `--joystick-monitor` and write down what each control number is; run `--joystick-calibrate` for the real min / max / centre; copy the axes you want into the device's profile with an `action` each; run `--joystick-monitor` again to check.

Test hooks (no hardware; also for scenarios): `--fake-joystick=<profile name, file stem or vid:pid>` creates a virtual device (8 axes, 32 buttons, 1 hat) that feeds the same pipeline; `--fake-axis=IDX:VALUE[,...]` (VALUE -1..1), `--fake-button=IDX[,...]`, `--fake-hat=0:MASK`. Example: `./space_game_v2 --fake-joystick=sidewinder --fake-axis=1:0.4,2:0.5,3:-1` flies with a half-twisted stick, pulled back, throttle forward (heading turns right, nose up, speed rises).

## Tuning
Too twitchy: raise `curve` (1.5-2) and `deadzone`. Ship drifts with the stick centred: raise `deadzone` or fix `centre` in `calibration`. Pedals never reach full or read something when unpressed: check `rest` (min / max) and `calibration.min/max`. A control works backwards: `invert`. Everything at once: the two settings above.

## Service
`core::IControllers` (`joystick_api.h`): `devices()`, `deviceCount()`, `rawAxis(dev, i)`, `rawButton`, `rawHat`, `lastEvent()`: for a controls settings screen and debug overlays.

## Not done here
* The pause menu's Settings page has no joystick rows yet (out of scope). Rows I would add: **Joystick input** (on/off toggle, `input.joystick_enabled`), **Joystick dead zone** and **Joystick sensitivity** (sliders, 0.5-2 and 0.1-4), and a **Controllers** page listing `IControllers::devices()` with live axis bars (`rawAxis`).
* No in-game rebinding UI and no auto-learning of axes (the two flags above are the workflow). `IControllers::lastEvent()` is reserved (always empty for now).
* Force feedback and the wheel's motor are not used.

## On the laptop (what to run)
```
cd ~/Documents/Space-Game-V2/space-game-v2 && export DISPLAY=:0 LD_LIBRARY_PATH=libs SDL_AUDIODRIVER=dummy
./space_game_v2 --list-joysticks                      # expect: PXN-V10 (11ff:3245) and Microsoft SideWinder (045e:003c), each with its profile
./space_game_v2 --joystick-monitor                    # turn the wheel, press each pedal, the shifter, the wheel buttons: note the numbers
./space_game_v2 --joystick-calibrate                  # 10 s, move everything to both ends; then: cat logs/joystick_calibration.json
```
