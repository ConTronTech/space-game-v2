// gameplay/blueprints - the set of unlocked blueprints (recipes with "requires_blueprint" need one, docs/BLUEPRINTS.md). Saved with the game.
// Unlocks come from exploration (world/anomalies), never from buying. Test flag: --give-blueprint=ID[,ID...].
#include "core/data_registry/data_api.h"
#include "core/save_system/save_api.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "gameplay/blueprints/blueprints_api.h"
#include "gameplay/blueprints/blueprints_rules.h"

class Blueprints : public engine::Module, public gameplay::IBlueprints, public core::ISaveable {
public:
    const char* name() const override { return "gameplay/blueprints"; }
    std::vector<std::string> dependencies() const override { return {}; }
    std::vector<std::string> optionalDependencies() const override { return {"core/save_system", "core/data_registry"}; }

    bool init(engine::Engine& eng) override {
        eng_ = &eng;
        data_ = eng.services.get<core::IData>();
        if ((saves_ = eng.services.get<core::ISaveSystem>())) saves_->registerSaveable(this);
        eng.services.provide<gameplay::IBlueprints>(this);
        std::string give = eng.flagValue("give-blueprint");
        size_t start = 0;
        while (!give.empty() && start <= give.size()) {                     // test flag: comma-separated ids
            size_t end = give.find(',', start);
            if (end == std::string::npos) end = give.size();
            std::string id = gameplay::blueprintId(give.substr(start, end - start));
            if (!id.empty()) { unlock(id); LOG_I("blueprints", "--give-blueprint: %s", id.c_str()); }
            start = end + 1;
        }
        LOG_I("blueprints", "%zu known blueprints, %zu unlocked", data_ ? data_->ids("blueprints").size() : (size_t)0, set_.size());
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        if (saves_) saves_->unregisterSaveable(this);
        eng.services.withdraw<gameplay::IBlueprints>();
    }

    // ---- gameplay::IBlueprints ----
    bool unlocked(const std::string& id) const override { return set_.unlocked(id); }
    bool unlock(const std::string& raw) override {
        std::string id = gameplay::blueprintId(raw);
        if (!set_.unlock(id)) return false;
        std::string nm = displayName(id);
        if (data_ && !data_->has("blueprints", id)) LOG_W("blueprints", "unlocked '%s', which data/blueprints.json does not list", id.c_str());
        LOG_I("blueprints", "BlueprintUnlocked: %s (%s)", id.c_str(), nm.c_str());
        eng_->events.emit(gameplay::BlueprintUnlocked{id, nm});
        return true;
    }
    void list(std::vector<std::string>& out) const override { out = set_.list(); }
    std::string displayName(const std::string& raw) const override {
        std::string id = gameplay::blueprintId(raw);
        return data_ && data_->has("blueprints", id) ? gameplay::blueprintName(id, data_->get("blueprints", id)) : gameplay::blueprintTitle(id);
    }

    // ---- core::ISaveable ----
    const char* saveId() const override { return "gameplay/blueprints"; }
    engine::Json save() const override { return set_.toJson(); }
    void load(const engine::Json& j) override {
        set_.fromJson(j);
        LOG_I("blueprints", "loaded: %zu unlocked", set_.size());
    }

private:
    engine::Engine* eng_ = nullptr;
    core::IData* data_ = nullptr;
    core::ISaveSystem* saves_ = nullptr;
    gameplay::BlueprintSet set_;
};

REGISTER_MODULE(Blueprints);
