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
- Slots are timestamped (`save_YYYYMMDD_HHMMSS`); the pause menu's Load page shows the newest 6. `--saves=<dir>` uses another folder.
- Writes are atomic (temp file + rename), so a crash mid-save never destroys the previous save.
- A bad, corrupt or too-new file is rejected **before** anything is touched, so the running game is never half-loaded.
- Saves from older or newer versions load fine: unknown modules in the file are ignored, and modules missing from
  the file keep their current state.
- Slot names allow only letters, digits, `_` and `-` (they become file names).
- Events: `core::GameSaved{slot}`, `core::GameLoaded{slot}` (after every module has loaded) - use `GameLoaded` to rebuild anything derived.
- Procedural worlds should save their **seed**, not their contents, and regenerate on load.
- When restoring position-like state, reset any interpolation history so nothing smears across the jump (see flight's `load`).
