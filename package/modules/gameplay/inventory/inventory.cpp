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
        base_ = std::max(1.0f, c.get("inventory.capacity", 100.0f, "cargo hold size in cargo units at upgrade level 0 (1 unit per ore unit)"));
        int startLevel = c.get("inventory.upgrade_level", 0, "cargo hold upgrade level (data/cargo.json); a saved game overrides it");
        levels_ = gameplay::defaultCargoLevels();
        if (auto* data = eng.services.get<core::IData>()) {
            auto ids = data->ids("cargo");
            if (!ids.empty()) {
                levels_.clear();
                for (auto& id : ids) levels_.push_back({(float)data->get("cargo", id)["capacity_mult"].num(1.0)});
            }
            for (auto& id : data->ids("items")) cargo_.setVolume(id, (float)data->get("items", id)["volume"].num(1.0));   // optional per-item volume
        }
        level_ = gameplay::clampLevel(startLevel, (int)levels_.size());
        refresh();
        eng_ = &eng;
        // test-only dev flags
        if (eng.hasFlag("clear-cargo")) cargo_.clear();
        std::string give = eng.flagValue("give");
        if (!give.empty()) {
            for (auto& s : gameplay::parseGive(give)) cargo_.add(s.id, s.amount);
            LOG_W("inventory", "--give: test cargo added (%s)", give.c_str());
        }
        if ((saves_ = eng.services.get<core::ISaveSystem>())) saves_->registerSaveable(this);
        eng.services.provide<gameplay::IInventory>(this);
        LOG_I("inventory", "hold level %d of %zu: %.0f cargo units, %.0f used", level_, levels_.size(), cargo_.capacity(), cargo_.used());
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
        LOG_D("inventory", "cargo %.0f / %.0f used:%s", cargo_.used(), cargo_.capacity(), s.empty() ? " (empty)" : s.c_str());
        (void)eng;
    }

    // ---- gameplay::IInventory ----
    float capacity() const override { return cargo_.capacity(); }
    float used() const override { return cargo_.used(); }
    float free() const override { return cargo_.free(); }
    int count(const std::string& id) const override { return cargo_.count(id); }
    int add(const std::string& id, int amount) override {
        int got = cargo_.add(id, amount);
        if (got > 0) eng_->events.emit(gameplay::InventoryChanged{id, got});
        if (amount > got && amount > 0 && !id.empty()) {
            LOG_I("inventory", "cargo full: %d %s refused (%d fitted, %.0f free)", amount - got, id.c_str(), got, cargo_.free());
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
    void setLevel(int level) override { level_ = gameplay::clampLevel(level, (int)levels_.size()); refresh(); LOG_I("inventory", "hold level %d: %.0f cargo units", level_, cargo_.capacity()); }

    // ---- saving: the stacks (in order) and the capacity level; a missing "stacks" key = empty ----
    const char* saveId() const override { return "gameplay/inventory"; }
    engine::Json save() const override {
        engine::Json arr = engine::Json::array();
        for (auto& s : cargo_.stacks()) arr.push(engine::Json::object().set("id", s.id).set("amount", s.amount));
        return engine::Json::object().set("level", level_).set("stacks", arr);
    }
    void load(const engine::Json& j) override {
        level_ = gameplay::clampLevel((int)j["level"].num(level_), (int)levels_.size());
        refresh();
        std::vector<gameplay::Stack> in;
        const engine::Json& arr = j["stacks"];
        for (size_t i = 0; i < arr.size(); i++) in.push_back({arr.at(i)["id"].str(), (int)arr.at(i)["amount"].num(0)});
        cargo_.assign(in);
        LOG_I("inventory", "loaded cargo: %zu stacks, %.0f / %.0f used", cargo_.stacks().size(), cargo_.used(), cargo_.capacity());
    }

private:
    void refresh() { cargo_.setCapacity(base_ * levels_[level_].capacityMult); }

    engine::Engine* eng_ = nullptr;
    core::ISaveSystem* saves_ = nullptr;
    gameplay::Cargo cargo_;
    std::vector<gameplay::CargoLevel> levels_;
    float base_ = 100.0f, summaryTimer_ = 0;
    int level_ = 0;
};

REGISTER_MODULE(Inventory);
