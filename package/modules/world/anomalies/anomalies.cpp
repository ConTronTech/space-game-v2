// world/anomalies - seeded points of interest, invisible until the Anomaly Scanner perk is installed; flying into one investigates it once
// (ore + a flavour-text toast). Provides world::IAnomalies (the cockpit radar draws the detected ones). Rules in anomaly_rules.h; design in docs/ANOMALIES.md.
#include <algorithm>
#include <cmath>
#include "core/data_registry/data_api.h"
#include "core/save_system/save_api.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "gameplay/inventory/inventory_api.h"
#include "ship/ship_core/ship_api.h"
#include "ui/toast/toast_api.h"
#include "world/anomalies/anomalies_api.h"
#include "world/anomalies/anomaly_rules.h"
#include "world/asteroids/asteroids_api.h"
#include "world/star_system/star_system_api.h"
#include "world/stations/stations_api.h"

namespace {
const std::string kPerk = "anomaly_scanner";
constexpr int kBeltSample = 64;      // asteroid positions offered to generation (evenly spaced indices: deterministic)

// Built-in kinds when data/anomalies.json is missing: the module still works, with one plain kind.
std::vector<world::AnomalyKind> fallbackKinds() {
    world::AnomalyKind k;
    k.id = "unknown_signal"; k.name = "Unknown Signal";
    k.messages = {"A faint signal fades as you arrive. Something was left behind."};
    k.rewards = {{"iron", 5, 10, 1}};
    return {k};
}
} // namespace

class Anomalies : public engine::Module, public world::IAnomalies, public core::ISaveable {
public:
    const char* name() const override { return "world/anomalies"; }
    std::vector<std::string> dependencies() const override { return {"world/star_system"}; }
    std::vector<std::string> optionalDependencies() const override {
        return {"core/data_registry", "core/save_system", "gameplay/inventory", "ui/toast", "ship/ship_core", "world/asteroids", "world/stations"};
    }

    bool init(engine::Engine& eng) override {
        auto& c = eng.config;
        if (!c.get("anomalies.enabled", true, "generate anomaly sites (found with the Anomaly Scanner, docs/ANOMALIES.md)")) return true;
        const unsigned seed = (unsigned)c.get("world.seed", 1234.0f, "seed of the star system (same seed = same system)");
        const int count = std::clamp(c.get("anomalies.count", 8, "number of anomaly sites, 0-64"), 0, 64);
        detectRange_ = std::max(0.0f, c.get("anomalies.detect_range", 4000.0f, "units: a site shows on the radar this close (x the kind's detect_mult), scanner perk only"));
        investigateRange_ = std::max(1.0f, c.get("anomalies.investigate_range", 75.0f, "units: flying this close to a detected site investigates it (once)"));
        sys_ = eng.services.get<world::IStarSystem>();
        if (!sys_) { LOG_W("anomalies", "no star system: no anomalies"); return true; }
        eng_ = &eng;

        if (const core::IData* data = eng.services.get<core::IData>())
            for (const auto& id : data->ids("anomalies")) kinds_.push_back(world::anomalyKindFromJson(id, data->get("anomalies", id)));
        if (kinds_.empty()) { LOG_W("anomalies", "no data/anomalies.json kinds: using one built-in kind"); kinds_ = fallbackKinds(); }

        world::AnomalyGenParams p;
        p.seed = seed; p.count = count; p.sun = sys_->sunPosition();
        for (const world::Body& b : sys_->bodies()) p.bodies.push_back({b.id, b.kind, b.radius, b.orbitRadius, b.parent});
        if (const world::IStations* st = eng.services.get<world::IStations>())
            for (int i = 0; i < st->count(); i++) {
                world::StationInfo in = st->info(i);
                world::Vec3d pp = sys_->positionAt(in.parent);
                p.stations.push_back({in.parent, world::adetail::dist(in.position, pp)});
            }
        if (const world::IAsteroids* ast = eng.services.get<world::IAsteroids>()) {
            int n = ast->count();
            for (int k = 0; k < kBeltSample && n > 0; k++) p.belt.push_back(ast->position((int)((long long)k * n / kBeltSample)));
        }
        for (const auto& k : kinds_) p.kindWeights.push_back(k.weight);
        sites_ = world::generateAnomalies(p);
        done_.assign(sites_.size(), false);
        detected_.assign(sites_.size(), false);
        positions_.resize(sites_.size());
        updatePositions();

        if (eng.hasFlag("give-anomaly-scanner")) {   // test flag: the perk without crafting
            forcedPerk_ = true;
            if (auto* inv = eng.services.get<gameplay::IInventory>()) inv->addPerk(kPerk);
            LOG_I("anomalies", "--give-anomaly-scanner: scanner perk set");
        }
        if (eng.hasFlag("anomaly-dump")) {
            static const char* zone[] = {"deep space", "near body", "belt"};
            for (const auto& s : sites_)
                LOG_I("anomalies", "site %d: %s (%s%s%s) at (%.0f, %.0f, %.0f)", s.id, kinds_[(size_t)s.kind].id.c_str(), zone[(int)s.zone],
                      s.anchor >= 0 ? ", " : "", s.anchor >= 0 ? sys_->bodies()[(size_t)s.anchor].name.c_str() : "",
                      positions_[(size_t)s.id].x, positions_[(size_t)s.id].y, positions_[(size_t)s.id].z);
        }
        if ((saves_ = eng.services.get<core::ISaveSystem>())) saves_->registerSaveable(this);
        eng.services.provide<world::IAnomalies>(this);
        active_ = true;
        LOG_I("anomalies", "%zu sites, %zu kinds, detect %.0f / investigate %.0f units", sites_.size(), kinds_.size(), detectRange_, investigateRange_);
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        if (!active_) return;
        if (saves_) saves_->unregisterSaveable(this);
        eng.services.withdraw<world::IAnomalies>();
    }

    void onUpdate(engine::Engine& eng, float) override {
        if (!active_) return;
        updatePositions();
        const ship::IShip* ship = eng.services.get<ship::IShip>();
        gameplay::IInventory* inv = eng.services.get<gameplay::IInventory>();
        const bool perk = forcedPerk_ || (inv && inv->hasPerk(kPerk));
        if (!ship || !perk) { std::fill(detected_.begin(), detected_.end(), false); return; }
        const engine::Vec3 sp = ship->position();
        for (size_t i = 0; i < sites_.size(); i++) {
            const world::Vec3d& p = positions_[i];
            double dx = p.x - sp.x, dy = p.y - sp.y, dz = p.z - sp.z, d = std::sqrt(dx * dx + dy * dy + dz * dz);
            const world::AnomalyKind& k = kinds_[(size_t)sites_[i].kind];
            detected_[i] = world::anomalyDetected(d, detectRange_, k.detectMul, perk, done_[i]);
            if (world::anomalyInvestigates(d, investigateRange_, perk, done_[i])) investigate(eng, i, inv);
        }
    }

    // ---- world::IAnomalies ----
    int count() const override { return (int)sites_.size(); }
    world::Vec3d position(int i) const override { return i >= 0 && i < (int)positions_.size() ? positions_[(size_t)i] : world::Vec3d{}; }
    bool detected(int i) const override { return i >= 0 && i < (int)detected_.size() && detected_[(size_t)i]; }
    bool investigated(int i) const override { return i >= 0 && i < (int)done_.size() && done_[(size_t)i]; }

    // ---- core::ISaveable: only WHICH ids are done; the sites themselves come back from the seed ----
    const char* saveId() const override { return "world/anomalies"; }
    engine::Json save() const override {
        engine::Json ids = engine::Json::array();
        for (int id : world::investigatedIds(done_)) ids.push(id);
        return engine::Json::object().set("investigated", ids);
    }
    void load(const engine::Json& j) override {
        const engine::Json& a = j["investigated"];
        std::vector<int> ids;
        for (size_t i = 0; i < a.size(); i++) ids.push_back((int)a.at(i).num(-1));
        done_ = world::investigatedMask(ids, (int)sites_.size());
        std::fill(detected_.begin(), detected_.end(), false);
        LOG_I("anomalies", "loaded: %zu of %zu sites investigated", world::investigatedIds(done_).size(), sites_.size());
    }

private:
    void updatePositions() {
        for (size_t i = 0; i < sites_.size(); i++) {
            const world::AnomalySite& s = sites_[i];
            positions_[i] = world::sitePosition(s, s.anchor >= 0 ? sys_->positionAt(s.anchor) : world::Vec3d{});
        }
    }

    void investigate(engine::Engine& eng, size_t i, gameplay::IInventory* inv) {
        done_[i] = true;
        detected_[i] = false;
        const world::AnomalySite& s = sites_[i];
        const world::AnomalyKind& k = kinds_[(size_t)s.kind];
        world::AnomalyRoll roll = world::rollAnomaly(k, s.seed);
        int got = 0;
        if (!roll.ore.empty()) {
            if (inv) got = inv->add(roll.ore, roll.amount);
            else LOG_W("anomalies", "site %d: no inventory, %d %s not given", s.id, roll.amount, roll.ore.c_str());
        }
        LOG_I("anomalies", "AnomalyInvestigated: site %d (%s): +%d %s (rolled %d)", s.id, k.id.c_str(), got, roll.ore.c_str(), roll.amount);
        eng.events.emit(world::AnomalyInvestigated{s.id, k.id, roll.ore, got});
        if (auto* t = eng.services.get<core::IToast>()) {
            std::string text = k.name + ": " + k.messages[(size_t)roll.message];
            if (!roll.ore.empty()) {
                std::string ore = roll.ore;
                if (const core::IData* data = eng.services.get<core::IData>()) ore = data->get("ores", roll.ore)["name"].str(roll.ore);
                for (auto& ch : ore) ch = (char)std::toupper((unsigned char)ch);
                text += "  +" + std::to_string(got) + " " + ore;
                if (got < roll.amount) text += " (hold full)";
            }
            t->show(text, core::IToast::Level::Info, 7.0f);
        }
    }

    engine::Engine* eng_ = nullptr;
    world::IStarSystem* sys_ = nullptr;
    core::ISaveSystem* saves_ = nullptr;
    bool active_ = false, forcedPerk_ = false;
    float detectRange_ = 4000, investigateRange_ = 75;
    std::vector<world::AnomalyKind> kinds_;
    std::vector<world::AnomalySite> sites_;
    std::vector<world::Vec3d> positions_;
    std::vector<bool> done_, detected_;
};

REGISTER_MODULE(Anomalies);
