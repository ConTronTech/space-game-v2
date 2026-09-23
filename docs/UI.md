# UI

`core/ui_handler` draws the 2D overlay. Modules register **panels** (ordered draw callbacks) and use the
glass toolkit. Pixel coordinates, origin top-left.

```cpp
auto& ui = eng.services.require<core::UIHandler>();
ui.addPanel("my_hud", 10, [](core::UIHandler& ui) {      // order: low = behind, high = on top
    ui.glass(16, 16, 200, 58);                             // frosted translucent panel
    ui.text(32, 22, "SPEED", 12, ui.theme.textDim);
    ui.textCentered(cx, y, "Title", 28, ui.theme.text);
    if (ui.button("Resume", x, y, w, h, /*focused*/ false)) { ... }        // true on click
    bool on = ui.toggle("Fullscreen", x, y, w, h, on, false);              // returns new value
    float v = ui.slider("Field of View", x, y, w, h, v, 60, 120, false);   // returns new value
});
```
Remove it in `shutdown`: `ui.removePanel("my_hud")`.

Widgets are immediate-mode: call them every frame and use the return value. `focused` is for keyboard/controller
focus; the mouse hover also highlights. They only react to the pointer while the cursor is free
(`ui.pointerFree()`), i.e. not while the mouse is captured for flight.

## Glass look
`glass()` = soft shadow + translucent gradient fill + light sheen over the top half + thin border + a glint
along the top edge. Things behind it stay visible. (It is see-through, not blurred.)

Tweak it in `config/ui/theme.json` (optional, every key optional, colors are `[r,g,b,a]` 0..1):
lower `glass_top` / `glass_bottom` alpha for more transparency, change `accent` for the highlight color,
`radius` for corner roundness. Changes apply on next launch.

Fonts: `assets/fonts/ui.ttf` if present, otherwise the system DejaVu Sans.

## Tabs and list rows (`core/ui_handler`)
```cpp
selected = ui.tabs({"CARGO", "CRAFTING"}, selected, x, y, w, h);   // a row of glass tab buttons; returns the new index (a click on a tab)
if (ui.listRow("Iron", "50", x, y, w, h, /*selected=*/false)) { ... }   // label left, value right; true on click
```
Like the other widgets they react only while the cursor is free. The layout math (equal widths, hit test, wrap-around cycling) is pure and unit-tested: `core/ui_handler/tabs_layout.h`. The game menu (docs/GAME_MENU.md) is the first user.

## Pause menu (`ui/pause_menu`)
Esc pauses the simulation (`Engine::setPaused`) and opens the menu: Resume / Settings (FOV slider, fullscreen) / Exit Game.
Mouse works, and so do the `ui_up`, `ui_down`, `ui_left`, `ui_right`, `ui_confirm` and `pause` actions from the input profile.

Pausing freezes only `onFixedUpdate`; UI, input and rendering keep running. Other modules can react:
```cpp
eng.events.subscribe<engine::PauseChanged>([](const engine::PauseChanged& e) { ... e.paused ... });
```
(the mouse module uses this to release the cursor while paused).

While the startup main menu (docs/MAIN_MENU.md) is open the pause menu neither draws nor reads Esc; the main menu borrows its Settings page through `ui::IPauseMenu::openSettingsOnly()` (Back returns to the main menu).

## Dev flags
`--paused` / `--paused=settings` start with the menu open. `--screenshot=out.bmp [--screenshot-frame=N]` saves a frame.

## Aspect ratios and small windows
Layout follows `core::uiLayoutScale(w, h)` = min(w/1280, h/720) clamped to 0.6..3 (1.0 at 1280x720; the HUD's `uiScale` is the same formula). `UIHandler::scale` holds it for the current frame; `button`, `toggle` and `slider` scale their labels, padding and knobs with it, and a panel should size itself as `size * ui.scale`.
The pause menu shows the pattern: scaled sizes, and a second column when a page does not fit the window height (the Settings page has up to 12 rows). Checked at 1280x1024 (5:4), 800x600 and 640x480 (4:3), 1366x768 and 1920x1080. See docs/DISPLAYS.md.

## Controllers screen (`ui/controller_setup`)
Pause menu > Settings > **Controllers** (or `--controller-setup[=live|setup|demo]` at start) opens a full glass panel (~80% of the window, `csetup::computeLayout` in `controller_setup_rules.h`, checked from 640x480 to 4K) at panel order 1100, above the pause menu. Three views as `ui.tabs()`: DEVICES (`listRow` per device), LIVE (`ui.bar` per axis, a glass box per button, the hat, a TEST strip of `IInput::value()` for yaw / thrust / brake / lift / roll / pitch, and `ui.slider`s for `input.joystick_deadzone_scale` and `input.joystick_sensitivity`) and SETUP (the guided wizard). While it is open the game is paused and the pause menu neither draws nor reads input (it checks `ui::IControllerSetup::isOpen()`); Esc closes it (the game menu's `PauseSwallow`). Details: docs/CONTROLLERS.md.
