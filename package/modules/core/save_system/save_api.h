#pragma once
// Save / load. Every module that has state worth keeping implements ISaveable and registers itself;
// the save system stores each one's JSON under its id. There is no central "game state" struct.
//
//     class Ship : public engine::Module, public core::ISaveable {
//         const char* saveId() const override { return "ship/core"; }
//         engine::Json save() const override { return engine::Json::object().set("hp", hp_); }
//         void load(const engine::Json& j) override { hp_ = (float)j["hp"].num(hp_); }   // missing keys keep current value
//         bool init(engine::Engine& e) override { if (auto* s = e.services.get<core::ISaveSystem>()) s->registerSaveable(this); ... }
//     };
// Saving/loading slots is done by menus:  saves.saveSlot(saves.newSlotName());  saves.loadSlot(name);
// The save system also tracks the session's *active slot* (the last one manually saved to or loaded): "Save" should
// overwrite it, "Save As" should pick a new name. The reserved slot kAutosaveSlot is written by autosave() (on a timer,
// save.autosave_interval_seconds) and never becomes the active slot, so an autosave never redirects a manual "Save".
#include <string>
#include <vector>
#include "engine/json.h"

namespace core {

struct GameSaved  { std::string slot; };
struct GameLoaded { std::string slot; };   // emitted after every module has loaded its part

class ISaveable {
public:
    virtual ~ISaveable() = default;
    virtual const char* saveId() const = 0;            // unique, e.g. "gameplay/inventory"
    virtual engine::Json save() const = 0;
    virtual void load(const engine::Json& data) = 0;   // must tolerate missing/extra keys (old saves, new versions)
};

struct SlotInfo {
    std::string name;       // file name without .json, e.g. "save_20260919_140211"
    long long time = 0;     // unix seconds when it was saved
};

class ISaveSystem {
public:
    static constexpr const char* kAutosaveSlot = "autosave";      // reserved: always overwritten, never the active slot

    virtual ~ISaveSystem() = default;
    virtual void registerSaveable(ISaveable* s) = 0;
    virtual void unregisterSaveable(ISaveable* s) = 0;
    virtual bool saveSlot(const std::string& name) = 0;            // false on error (see log). Success makes `name` the active slot (not for kAutosaveSlot)
    virtual bool loadSlot(const std::string& name) = 0;            // false leaves the running game untouched. Success makes `name` the active slot (not for kAutosaveSlot)
    virtual std::vector<SlotInfo> listSlots() const = 0;           // newest first
    virtual bool deleteSlot(const std::string& name) = 0;          // deleting the active slot clears activeSlot()
    virtual std::string newSlotName() const = 0;                   // unique, timestamped

    // Session state (not persisted: a new process starts with no active slot and both times 0).
    virtual const std::string& activeSlot() const = 0;             // "" = none yet: "Save" should then behave like "Save As"
    virtual long long lastSaveTime() const = 0;                    // unix seconds of the last successful save (manual or autosave), 0 = none
    virtual long long lastLoadTime() const = 0;                    // unix seconds of the last successful load, 0 = none
    virtual bool autosave() = 0;                                   // writes kAutosaveSlot; does NOT change activeSlot()
};

} // namespace core
