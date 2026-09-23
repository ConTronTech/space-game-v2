// gameplay/inventory - the ship's cargo hold (ISaveable, IInventory): one unified slot grid. Rules in inventory_rules.h; model, save format and items in docs/INVENTORY.md.
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
        int cols = c.get("inventory.grid_columns", gameplay::kDefaultGridCols, "cargo grid width in slots (the slot count never changes with the upgrade level)");
        int rows = c.get("inventory.grid_rows", gameplay::kDefaultGridRows, "cargo grid height in slots");
        int startLevel = c.get("inventory.upgrade_level", 0, "cargo hold upgrade level (data/cargo.json); a saved game overrides it");
        cargo_.setGrid(std::clamp(cols, 1, 32), std::clamp(rows, 1, 32));
        levels_ = gameplay::defaultCargoLevels();
        std::vector<gameplay::ResourceDef> res = gameplay::defaultResources();
        if (auto* data = eng.services.get<core::IData>()) {
            auto ids = data->ids("cargo");
            if (!ids.empty()) {
                levels_.clear();
                for (auto& id : ids) {
                    const engine::Json& l = data->get("cargo", id);
                    double old = l["resource_mult"].num(l["capacity_mult"].num(1.0));     // older files: the per-ore multiplier is the fallback
                    levels_.push_back({(float)l["stack_mult"].num(old)});
                }
            }
            auto ores = data->ids("ores");
            if (!ores.empty()) {
                res.clear();
                for (auto& id : ores) res.push_back({id, std::max(0, (int)data->get("ores", id)["cargo_cap"].num(gameplay::kDefaultResourceCap)), true});
            }
            for (auto& id : data->ids("items")) {                                           // optional per-item stack_cap (or derived from volume)
                const engine::Json& it = data->get("items", id);
                cargo_.setItemStack(id, gameplay::itemBaseStack(it["stack_cap"].num(0), it["volume"].num(1.0)));
            }
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
                if (got < s.amount) LOG_W("inventory", "--give: only %d of %d %s fit the grid", got, s.amount, s.id.c_str());
            }
            LOG_W("inventory", "--give: test cargo added (%s)", give.c_str());
        }
        if ((saves_ = eng.services.get<core::ISaveSystem>())) saves_->registerSaveable(this);
        eng.services.provide<gameplay::IInventory>(this);
        LOG_I("inventory", "cargo grid %dx%d (%d slots), level %d of %zu (stack x%.2f), %d slots used", cargo_.columns(), cargo_.rows(), cargo_.slotCount(), level_,
              levels_.size(), cargo_.stackMult(), cargo_.usedSlots());
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
        for (auto& st : cargo_.totals()) s += " " + st.id + " " + std::to_string(st.amount);
        LOG_D("inventory", "cargo %d / %d slots:%s", cargo_.usedSlots(), cargo_.slotCount(), s.empty() ? " (empty)" : s.c_str());
        (void)eng;
    }

    // ---- gameplay::IInventory ----
    float capacity() const override { return (float)cargo_.slotCount(); }
    float used() const override { return (float)cargo_.usedSlots(); }
    float free() const override { return (float)cargo_.freeSlots(); }
    float capacity(const std::string& id) const override { return (float)cargo_.count(id) + (float)cargo_.room(id); }
    float free(const std::string& id) const override { return (float)cargo_.room(id); }
    bool isResource(const std::string& id) const override { return cargo_.isResource(id); }
    void resources(std::vector<std::string>& out) const override { cargo_.countedResources(out); }
    int count(const std::string& id) const override { return cargo_.count(id); }
    int add(const std::string& id, int amount) override {
        int got = cargo_.add(id, amount);
        if (got > 0) eng_->events.emit(gameplay::InventoryChanged{id, got});
        if (amount > got && amount > 0 && !id.empty()) {
            LOG_I("inventory", "cargo full for %s: %d refused (%d fitted, %d free slots)", id.c_str(), amount - got, got, cargo_.freeSlots());
            eng_->events.emit(gameplay::CargoFull{id, amount - got});
        }
        return got;
    }
    bool remove(const std::string& id, int amount) override {
        if (!cargo_.remove(id, amount)) return false;
        eng_->events.emit(gameplay::InventoryChanged{id, -amount});
        return true;
    }
    void stacks(std::vector<gameplay::Stack>& out) const override { out = cargo_.totals(); }
    int level() const override { return level_; }
    int levelCount() const override { return (int)levels_.size(); }
    bool hasPerk(const std::string& id) const override { return perks_.has(id); }
    bool addPerk(const std::string& id) override {
        if (!perks_.add(id)) return false;
        LOG_I("inventory", "perk '%s' installed", id.c_str());
        return true;
    }
    void setLevel(int level) override { level_ = gameplay::clampLevel(level, (int)levels_.size()); refresh(); LOG_I("inventory", "hold level %d: stacks x%.2f", level_, cargo_.stackMult()); }

    // ---- the grid ----
    int slotCount() const override { return cargo_.slotCount(); }
    int gridColumns() const override { return cargo_.columns(); }
    void slots(std::vector<gameplay::Stack>& out) const override { out = cargo_.slots(); }
    int stackCap(const std::string& id) const override { return cargo_.stackCap(id); }
    int stackCapAtLevel(const std::string& id, int level) const override {
        if (levels_.empty()) return cargo_.stackCap(id);
        return cargo_.stackCapAt(id, levels_[(size_t)gameplay::clampLevel(level, (int)levels_.size())].stackMult);
    }
    int discardSlot(int slot, int amount) override {
        std::string id;
        int n = cargo_.discardSlot(slot, amount, &id);
        if (n > 0) eng_->events.emit(gameplay::InventoryChanged{id, -n});
        return n;
    }
    bool moveSlot(int from, int to) override { return cargo_.moveSlot(from, to); }
    int roomAfter(const std::vector<gameplay::Stack>& removed, const std::string& id) const override { return cargo_.roomAfter(removed, id); }

    // ---- saving: version 3 = the grid layout ("slots": [{slot, id, amount}]) plus a merged "stacks" list (what versions 1 and 2 used, so an older
    //      build still reads its cargo). Loading a v3 save puts every stack back into its slot; a v1 / v2 save (no "slots") is auto-stacked.
    //      Anything that does not fit is clipped with a warning. A missing "stacks"/"slots" key = empty. ----
    const char* saveId() const override { return "gameplay/inventory"; }
    engine::Json save() const override {
        engine::Json sl = engine::Json::array();
        const auto& v = cargo_.slots();
        for (size_t i = 0; i < v.size(); i++)
            if (!v[i].id.empty()) sl.push(engine::Json::object().set("slot", (int)i).set("id", v[i].id).set("amount", v[i].amount));
        engine::Json arr = engine::Json::array();
        for (auto& s : cargo_.totals()) arr.push(engine::Json::object().set("id", s.id).set("amount", s.amount));
        engine::Json perks = engine::Json::array();
        for (auto& p : perks_.ids) perks.push(p);
        return engine::Json::object().set("version", gameplay::kSaveVersion).set("level", level_).set("slots", sl).set("stacks", arr).set("perks", perks);
    }
    void load(const engine::Json& j) override {
        int version = (int)j["version"].num(1);
        level_ = gameplay::clampLevel((int)j["level"].num(level_), (int)levels_.size());
        refresh();
        std::vector<gameplay::Stack> clipped;
        const engine::Json& sl = j["slots"];
        if (sl.isArray()) {
            std::vector<gameplay::Cargo::PlacedStack> in;
            for (size_t i = 0; i < sl.size(); i++) in.push_back({(int)sl.at(i)["slot"].num(-1), {sl.at(i)["id"].str(), (int)sl.at(i)["amount"].num(0)}});
            cargo_.assignSlots(in, &clipped);
        } else {
            std::vector<gameplay::Stack> in;
            const engine::Json& arr = j["stacks"];
            for (size_t i = 0; i < arr.size(); i++) in.push_back({arr.at(i)["id"].str(), (int)arr.at(i)["amount"].num(0)});
            cargo_.assign(in, &clipped);
        }
        perks_ = gameplay::Perks{};                                       // a save without "perks" (older) = none
        const engine::Json& pk = j["perks"];
        for (size_t k = 0; k < pk.size(); k++) perks_.add(pk.at(k).str());
        for (auto& c : clipped) LOG_W("inventory", "loading a version %d save: %d %s did not fit the cargo grid and was dropped", version, c.amount, c.id.c_str());
        LOG_I("inventory", "loaded cargo (save version %d): %d / %d slots used", version, cargo_.usedSlots(), cargo_.slotCount());
    }

private:
    void refresh() { cargo_.setStackMult(levels_.empty() ? 1.0f : levels_[(size_t)level_].stackMult); }

    engine::Engine* eng_ = nullptr;
    core::ISaveSystem* saves_ = nullptr;
    gameplay::Cargo cargo_;
    gameplay::Perks perks_;
    std::vector<gameplay::CargoLevel> levels_;
    float summaryTimer_ = 0;
    int level_ = 0;
};

REGISTER_MODULE(Inventory);
