# Saving and loading

No global game state. Every module that has state worth keeping implements `core::ISaveable` and registers with
`core::ISaveSystem`; the save system stores each module's JSON under its id.

```cpp
class Inventory : public engine::Module, public core::ISaveable {
    const char* saveId() const override { return "gameplay/inventory"; }        // unique
    engine::Json save() const override {
        return engine::Json::object().set("iron", iron_).set("gold", gold_);
    }
    void load(const engine::Json& j) override {
        iron_ = (int)j["iron"].num(iron_);      // missing key -> keep the current value (old saves keep working)
        gold_ = (int)j["gold"].num(gold_);
    }
    bool init(engine::Engine& e) override {
        if ((saves_ = e.services.get<core::ISaveSystem>())) saves_->registerSaveable(this);
        return true;
    }
    void shutdown(engine::Engine&) override { if (saves_) saves_->unregisterSaveable(this); }
    core::ISaveSystem* saves_ = nullptr;
};
```

- Save files: `saves/<slot>.json` = `{ "format": 1, "time": <unix>, "modules": { "<saveId>": {...} } }`. Git-ignored.
- New slots get timestamped names (`save_YYYYMMDD_HHMMSS`, from `newSlotName()`); the pause menu's Load page shows the newest 6 (the autosave is labelled "Autosave"). `--saves=<dir>` uses another folder.
- Writes are atomic (temp file + rename), so a crash mid-save never destroys the previous save.
- A bad, corrupt or too-new file is rejected **before** anything is touched, so the running game is never half-loaded.
- Saves from older or newer versions load fine: unknown modules in the file are ignored, and modules missing from
  the file keep their current state.
- Slot names allow only letters, digits, `_` and `-` (they become file names).
- Events: `core::GameSaved{slot}`, `core::GameLoaded{slot}` (after every module has loaded) - use `GameLoaded` to rebuild anything derived.
- Procedural worlds should save their **seed**, not their contents, and regenerate on load.
- When restoring position-like state, reset any interpolation history so nothing smears across the jump (see flight's `load`).

## Active slot, Save vs Save As, autosave
The session's save state lives in `core/save_system` itself (not in a menu), so the pause menu and a future main menu
see the same thing:

| `ISaveSystem` call | Meaning |
|---|---|
| `activeSlot()` | the slot a manual "Save" overwrites: the last slot successfully saved to or loaded. `""` = none (fresh process, after loading the autosave, or after the active slot was deleted) |
| `lastSaveTime()` / `lastLoadTime()` | unix seconds of the last successful save (manual **or** autosave) / load; 0 = none this session |
| `autosave()` | writes the reserved slot `ISaveSystem::kAutosaveSlot` (`"autosave"`), always overwriting it |

- Session state only: nothing about the active slot is written to disk. A new process starts with no active slot.
- `saveSlot()` / `loadSlot()` set the active slot only on success, and never to `"autosave"`: an autosave must not
  redirect where "Save" goes. Loading the autosave clears the active slot, so the next "Save" makes a new slot.
- Autosave timer: every `save.autosave_interval_seconds` (config tunable, default 300, `0` = off) of **simulation**
  time; it runs in the save system's `onFixedUpdate`, so time spent paused does not count. Any successful save or load
  restarts the countdown; a failed autosave is logged and retried one interval later.
- Pause menu: **Save Game** overwrites `activeSlot()` (with no active slot it behaves like Save As). **Save As...**
  always writes a new slot and makes it active. There is no text-entry widget yet, so Save As uses an auto-generated
  name; the menu's `saveAsName_` field is the hook a name field will fill later.

## What is saved today
| Save id | Content |
|---|---|
| `gameplay/flight` | ship pose and stats (docs/SHIP.md) |
| `ship/warp_drive` | upgrade level |
| `world/star_system` | seed and simulation time |
| `gameplay/inventory` | `{"level": n, "stacks": [{"id": "iron", "amount": 50}, ...]}`: the cargo hold in stack order and its capacity level; a missing `stacks` key = an empty hold (docs/INVENTORY.md) |
| `gameplay/blueprints` | `{"unlocked": [ids]}`: unlocked blueprints (docs/BLUEPRINTS.md) |
| `world/anomalies` | `{"investigated": [ids]}`: which anomaly sites are done; the sites come back from the seed (docs/ANOMALIES.md) |

Known gaps (Phase 4.3 / later): ore chunks in flight and destroyed asteroids are **not** saved (the asteroid field regenerates on load).

## Inventory save version 2
`gameplay/inventory` writes `{"version":3,"level":n,"slots":[{slot,id,amount}],"stacks":[{id,amount}],"perks":[...]}`: `slots` is the cargo grid layout (restored slot by slot), `stacks` the per-id totals (kept so an older build can still read it). A version 1/2 save (no `slots`) is auto-stacked into the grid. Anything that does not fit (a bad slot index, a stack over its cap, a full grid) is re-stacked and, if still no room, clipped with a logged warning per stack; it never crashes.
