#pragma once
// core/save_system - implementation of core::ISaveSystem. Files: saves/<slot>.json
//   { "format": 1, "time": <unix>, "modules": { "<saveId>": {...}, ... } }
#include <functional>
#include <string>
#include <vector>
#include "core/save_system/save_api.h"
#include "engine/module.h"

namespace core {

class SaveSystem : public engine::Module, public ISaveSystem {
public:
    static constexpr int kFormat = 1;

    const char* name() const override { return "core/save_system"; }
    int priority() const override { return -1400; }   // early, so saveable modules can register in their init()
    bool init(engine::Engine&) override;
    void shutdown(engine::Engine&) override;

    void registerSaveable(ISaveable* s) override;
    void unregisterSaveable(ISaveable* s) override;
    bool saveSlot(const std::string& name) override;
    bool loadSlot(const std::string& name) override;
    std::vector<SlotInfo> listSlots() const override;
    bool deleteSlot(const std::string& name) override;
    std::string newSlotName() const override;

    const std::string& activeSlot() const override { return active_; }
    long long lastSaveTime() const override { return lastSave_; }
    long long lastLoadTime() const override { return lastLoad_; }
    bool autosave() override;

    // Autosave timer: counts simulation time (fixed steps stop while paused), so time in the pause menu does not count.
    // Any successful save or load restarts the countdown. A failed autosave also restarts it (no retry spam every step).
    void onFixedUpdate(engine::Engine&, float dt) override;

    void setDir(const std::string& d) { dir_ = d; }                       // default "saves" (or --saves=<dir>)
    void setClock(std::function<long long()> c) { clock_ = std::move(c); } // tests
    void setAutosaveInterval(float seconds) { autosaveInterval_ = seconds; autosaveTimer_ = 0; }   // tests; <= 0 disables
    float autosaveInterval() const { return autosaveInterval_; }

    static bool validSlotName(const std::string& n);

private:
    std::string pathFor(const std::string& slot) const { return dir_ + "/" + slot + ".json"; }
    long long now() const;
    bool writeSlot(const std::string& name);   // the actual save; saveSlot/autosave add the active-slot bookkeeping

    std::string dir_ = "saves";
    std::string active_;
    long long lastSave_ = 0, lastLoad_ = 0;
    float autosaveInterval_ = 300.0f, autosaveTimer_ = 0.0f;
    std::vector<ISaveable*> saveables_;
    std::function<long long()> clock_;
    engine::Engine* eng_ = nullptr;
};

} // namespace core
