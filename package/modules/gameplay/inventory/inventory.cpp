// gameplay/inventory - the ship's cargo hold (ISaveable, IInventory). Rules in inventory_rules.h; model, save format and items in docs/INVENTORY.md.
#include <cstdlib>
#include "core/data_registry/data_api.h"
#include "core/save_system/save_api.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "gameplay/inventory/inventory_api.h"
#include "gameplay/inventory/inventory_rules.h"

class Inventory : public engine::Module, public gameplay::IInventory, public core::ISaveable {
public:
    const char* name() const override { return "gameplay/inventory"; }
    std::vector<std::string> optionalDependencies() const override { return {"core/data_registry", "core/save_system"}; }

    bool init(engine::Engine& eng) override {
        auto& c = eng.config;
        generalBase_ = std::max(0.0f, c.get("inventory.general_capacity", 30.0f, "the general hold (crafted items) in volume units at upgrade level 0; ores have their own holds (data/ores.json cargo_cap)"));
        int startLevel = c.get("inventory.upgrade_level", 0, "cargo hold upgrade level (data/cargo.json); a saved game overrides it");
        levels_ = gameplay::defaultCargoLevels();
        std::vector<gameplay::ResourceDef> res = gameplay::defaultResources();
        if (auto* data = eng.services.get<core::IData>()) {
            auto ids = data->ids("cargo");
            if (!ids.empty()) {
                levels_.clear();
                for (auto& id : ids) {
                    const engine::Json& l = data->get("cargo", id);
                    float old = (float)l["capacity_mult"].num(1.0);                       // the old single multiplier is the fallback for both
                    levels_.push_back({(float)l["resource_mult"].num(old), (float)l["general_mult"].num(old)});
                }
            }
            auto ores = data->ids("ores");
            if (!ores.empty()) {
                res.clear();
                for (auto& id : ores) res.push_back({id, std::max(0, (int)data->get("ores", id)["cargo_cap"].num(gameplay::kDefaultResourceCap)), true});
            }
            for (auto& id : data->ids("items")) cargo_.setVolume(id, (float)data->get("items", id)["volume"].num(1.0));   // optional per-item volume
        }
        cargo_.setResources(res);
        level_ = gameplay::clampLevel(startLevel, (int)levels_.size());
        refresh();
        eng_ = &eng;
        // test-only dev flags
        if (eng.hasFlag("clear-cargo")) cargo_.clear();
        std::string give = eng.flagValue("give");
        if (!give.empty()) {
            for (auto& s : gameplay::parseGive(give)) {
                int got = cargo_.add(s.id, s.amount);
                if (got < s.amount) LOG_W("inventory", "--give: only %d of %d %s fit its hold", got, s.amount, s.id.c_str());
            }
            LOG_W("inventory", "--give: test cargo added (%s)", give.c_str());
        }
        if ((saves_ = eng.services.get<core::ISaveSystem>())) saves_->registerSaveable(this);
        eng.services.provide<gameplay::IInventory>(this);
        LOG_I("inventory", "hold level %d of %zu: %zu resource holds + general hold %.0f, total %.0f units, %.0f used", level_, levels_.size(), res.size(), cargo_.generalCapacity(),
              cargo_.totalCapacity(), cargo_.totalUsed());
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        if (saves_) saves_->unregisterSaveable(this);
        eng.services.withdraw<gameplay::IInventory>();
    }

    void onFixedUpdate(engine::Engine& eng, float dt) override {
        if ((summaryTimer_ += dt) < 5.0f) return;                         // a cargo summary every 5 s at debug level
        summaryTimer_ = 0;
        std::string s;
        for (auto& st : cargo_.stacks()) s += " " + st.id + " " + std::to_string(st.amount);
        LOG_D("inventory", "cargo %.0f / %.0f used:%s", cargo_.totalUsed(), cargo_.totalCapacity(), s.empty() ? " (empty)" : s.c_str());
        (void)eng;
    }

    // ---- gameplay::IInventory ----
    float capacity() const override { return cargo_.totalCapacity(); }
    float used() const override { return cargo_.totalUsed(); }
    float free() const override { return std::max(0.0f, cargo_.totalCapacity() - cargo_.totalUsed()); }
    float capacity(const std::string& id) const override { return cargo_.capacity(id); }
    float free(const std::string& id) const override { return cargo_.free(id); }
    bool isResource(const std::string& id) const override { return cargo_.isResource(id); }
    void resources(std::vector<std::string>& out) const override { cargo_.countedResources(out); }
    float generalCapacity() const override { return cargo_.generalCapacity(); }
    float generalUsed() const override { return cargo_.generalUsed(); }
    int count(const std::string& id) const override { return cargo_.count(id); }
    int add(const std::string& id, int amount) override {
        int got = cargo_.add(id, amount);
        if (got > 0) eng_->events.emit(gameplay::InventoryChanged{id, got});
        if (amount > got && amount > 0 && !id.empty()) {
            LOG_I("inventory", "%s hold full: %d %s refused (%d fitted, %.0f free in that hold)", id.c_str(), amount - got, id.c_str(), got, cargo_.free(id));
            eng_->events.emit(gameplay::CargoFull{id, amount - got});
        }
        return got;
    }
    bool remove(const std::string& id, int amount) override {
        if (!cargo_.remove(id, amount)) return false;
        eng_->events.emit(gameplay::InventoryChanged{id, -amount});
        return true;
    }
    void stacks(std::vector<gameplay::Stack>& out) const override { out = cargo_.stacks(); }
    int level() const override { return level_; }
    int levelCount() const override { return (int)levels_.size(); }
    void setLevel(int level) override { level_ = gameplay::clampLevel(level, (int)levels_.size()); refresh(); LOG_I("inventory", "hold level %d: %.0f units in total", level_, cargo_.totalCapacity()); }

    // ---- saving: the stacks (in order) and the capacity level. Version 2 = per-resource pools; version 1 (no "version" key) = the old single pool.
    //      Both load the same way: every stack goes into its pool and anything over a pool's room is clipped with a warning. A missing "stacks" key = empty. ----
    const char* saveId() const override { return "gameplay/inventory"; }
    engine::Json save() const override {
        engine::Json arr = engine::Json::array();
        for (auto& s : cargo_.stacks()) arr.push(engine::Json::object().set("id", s.id).set("amount", s.amount));
        return engine::Json::object().set("version", gameplay::kSaveVersion).set("level", level_).set("stacks", arr);
    }
    void load(const engine::Json& j) override {
        int version = (int)j["version"].num(1);
        level_ = gameplay::clampLevel((int)j["level"].num(level_), (int)levels_.size());
        refresh();
        std::vector<gameplay::Stack> in, clipped;
        const engine::Json& arr = j["stacks"];
        for (size_t i = 0; i < arr.size(); i++) in.push_back({arr.at(i)["id"].str(), (int)arr.at(i)["amount"].num(0)});
        cargo_.assign(in, &clipped);
        for (auto& c : clipped) LOG_W("inventory", "loading a version %d save: %d %s did not fit its hold and was dropped", version, c.amount, c.id.c_str());
        LOG_I("inventory", "loaded cargo (save version %d): %zu stacks, %.0f / %.0f used", version, cargo_.stacks().size(), cargo_.totalUsed(), cargo_.totalCapacity());
    }

private:
    void refresh() { cargo_.setGeneralBase(generalBase_); cargo_.setMultipliers(levels_[level_].resourceMult, levels_[level_].generalMult); }

    engine::Engine* eng_ = nullptr;
    core::ISaveSystem* saves_ = nullptr;
    gameplay::Cargo cargo_;
    std::vector<gameplay::CargoLevel> levels_;
    float generalBase_ = 30.0f, summaryTimer_ = 0;
    int level_ = 0;
};

REGISTER_MODULE(Inventory);
