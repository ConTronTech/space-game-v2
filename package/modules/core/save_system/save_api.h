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
    virtual ~ISaveSystem() = default;
    virtual void registerSaveable(ISaveable* s) = 0;
    virtual void unregisterSaveable(ISaveable* s) = 0;
    virtual bool saveSlot(const std::string& name) = 0;            // false on error (see log)
    virtual bool loadSlot(const std::string& name) = 0;            // false leaves the running game untouched
    virtual std::vector<SlotInfo> listSlots() const = 0;           // newest first
    virtual bool deleteSlot(const std::string& name) = 0;
    virtual std::string newSlotName() const = 0;                   // unique, timestamped
};

} // namespace core
