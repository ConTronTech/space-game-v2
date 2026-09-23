// world/distress_beacons - a recurring, timed distress signal: every so often (seeded gap) a beacon appears somewhere in the system (deep space,
// near a body or in the belt - the world/anomalies zones), always shown within a large detect range, and goes quiet after its lifetime. Flying
// to it before then recovers a black box plus a little ore. Provides world::IDistressBeacons. Rules in beacon_rules.h; design in
// docs/DISTRESS_BEACONS.md. No save: the next beacon is re-rolled from the seed on load; an active one is simply gone after a reload.
#include <algorithm>
#include <cctype>
#include <cmath>
#include "core/data_registry/data_api.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "gameplay/inventory/inventory_api.h"
#include "ship/ship_core/ship_api.h"
#include "ui/toast/toast_api.h"
#include "world/asteroids/asteroids_api.h"
#include "world/distress_beacons/beacon_rules.h"
#include "world/distress_beacons/distress_beacons_api.h"
#include "world/star_system/star_system_api.h"
#include "world/stations/stations_api.h"

namespace {
const std::string kBlackBox = "black_box";
constexpr int kBeltSample = 64;      // asteroid positions offered to the site pick (evenly spaced indices: deterministic)
} // namespace

class DistressBeacons : public engine::Module, public world::IDistressBeacons {
public:
    const char* name() const override { return "world/distress_beacons"; }
    std::vector<std::string> dependencies() const override { return {"world/star_system"}; }
    std::vector<std::string> optionalDependencies() const override {
        return {"core/data_registry", "gameplay/inventory", "ui/toast", "ship/ship_core", "world/asteroids", "world/stations"};
    }

    bool init(engine::Engine& eng) override {
        auto& c = eng.config;
        if (!c.get("distress_beacons.enabled", true, "a recurring, timed distress signal to fly to for a black box + ore (docs/DISTRESS_BEACONS.md)")) return true;
        sys_ = eng.services.get<world::IStarSystem>();
        if (!sys_) { LOG_W("distress_beacons", "no star system: no distress beacons"); return true; }

        world::BeaconParams p;
        p.intervalMin = c.get("distress_beacons.interval_min", 60.0f, "seconds: shortest gap between one beacon ending (found or expired) and the next");
        p.intervalMax = c.get("distress_beacons.interval_max", 180.0f, "seconds: longest gap (each gap is a seeded roll in [min, max])");
        p.lifetime = c.get("distress_beacons.lifetime_seconds", 120.0f, "seconds an unclaimed beacon keeps broadcasting before it goes quiet");
        p.detectRange = c.get("distress_beacons.detect_range", 150000.0f, "units: the beacon is shown within this (no perk needed; spawns prefer a site inside it)");
        p.investigateRange = c.get("distress_beacons.investigate_range", 75.0f, "units: flying this close to the beacon recovers its black box");
        params_ = world::sanitizeBeaconParams(p);
        oreId_ = c.get("distress_beacons.ore_reward_id", std::string("cobalt"), "ore id given with the black box (data/ores.json), \"\" = none");
        oreAmount_ = std::clamp(c.get("distress_beacons.ore_reward_amount", 10, "units of ore_reward_id given with the black box"), 0, 1000);

        world_.sun = sys_->sunPosition();
        for (const world::Body& b : sys_->bodies()) world_.bodies.push_back({b.id, b.kind, b.radius, b.orbitRadius, b.parent});
        if (const world::IStations* st = eng.services.get<world::IStations>())
            for (int i = 0; i < st->count(); i++) {
                world::StationInfo in = st->info(i);
                world_.stations.push_back({in.parent, world::bdetail::dist(in.position, sys_->positionAt(in.parent))});
            }
        if (const world::IAsteroids* ast = eng.services.get<world::IAsteroids>()) {
            const int n = ast->count();
            for (int k = 0; k < kBeltSample && n > 0; k++) world_.belt.push_back(ast->position((int)((long long)k * n / kBeltSample)));
        }

        const unsigned seed = (unsigned)c.get("world.seed", 1234.0f, "seed of the star system (same seed = same system)");
        state_ = world::startBeaconCycle(seed ^ 0xD157E55u, params_);
        if (eng.hasFlag("distress-beacon-now")) {   // test flag: the first beacon appears right away
            state_.untilSpawn = 1.0;
            LOG_I("distress_beacons", "--distress-beacon-now: first beacon in 1s");
        }
        eng.services.provide<world::IDistressBeacons>(this);
        on_ = true;
        LOG_I("distress_beacons", "first beacon in %.0fs; interval %.0f-%.0fs, lifetime %.0fs, detect %.0f / investigate %.0f units, reward black box + %d %s",
              state_.untilSpawn, params_.intervalMin, params_.intervalMax, params_.lifetime, params_.detectRange, params_.investigateRange,
              oreAmount_, oreId_.c_str());
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        if (on_) eng.services.withdraw<world::IDistressBeacons>();
        on_ = false;
    }

    // Fixed step: frozen while paused, like the rest of the simulation.
    void onFixedUpdate(engine::Engine& eng, float dt) override {
        if (!on_) return;
        const world::BeaconStep st = world::stepBeacon(state_, params_, dt);
        const ship::IShip* ship = eng.services.get<ship::IShip>();
        const engine::Vec3d sp = ship ? ship->positionD() : engine::Vec3d{world_.sun.x, world_.sun.y, world_.sun.z};
        if (st.spawned) spawn(eng, {sp.x, sp.y, sp.z});
        if (st.expired) {
            detected_ = false;
            LOG_I("distress_beacons", "DistressBeaconExpired; next beacon in %.0fs", state_.untilSpawn);
            eng.events.emit(world::DistressBeaconExpired{});
            if (auto* t = eng.services.get<core::IToast>()) t->show("The distress signal has gone quiet.", core::IToast::Level::Info, 5.0f);
            return;
        }
        if (!state_.active) return;
        pos_ = world::beaconPosition(site_, site_.anchor >= 0 ? sys_->positionAt(site_.anchor) : world::Vec3d{});
        if (!ship || !ship->status().alive) { detected_ = false; return; }
        const double d = world::bdetail::dist(pos_, {sp.x, sp.y, sp.z});
        detected_ = world::beaconDetected(d, params_.detectRange, true);
        if (world::beaconInvestigates(d, params_.investigateRange, true)) investigate(eng);
    }

    // ---- world::IDistressBeacons ----
    bool active() const override { return on_ && state_.active; }
    world::Vec3d position() const override { return pos_; }
    float etaExpirySeconds() const override { return on_ ? (float)world::beaconEta(state_) : -1.0f; }
    bool detected() const override { return active() && detected_; }

private:
    // A new beacon: roll its candidate sites (the anomalies zones), keep a reachable one, announce it.
    void spawn(engine::Engine& eng, const world::Vec3d& ship) {
        siteSeed_ = world::beaconSiteSeed(state_.seed, state_.cycle);
        const std::vector<world::BeaconSite> cands = world::beaconCandidates(world_, siteSeed_);
        std::vector<world::Vec3d> where;
        for (const auto& s : cands) where.push_back(world::beaconPosition(s, s.anchor >= 0 ? sys_->positionAt(s.anchor) : world::Vec3d{}));
        const int pick = world::pickBeaconCandidate(where, ship, params_.detectRange, params_.investigateRange, siteSeed_);
        site_ = cands[(size_t)std::max(0, pick)];
        pos_ = where[(size_t)std::max(0, pick)];
        static const char* zone[] = {"deep space", "near body", "belt"};
        LOG_I("distress_beacons", "DistressBeaconSpawned (beacon %u, %s) at (%.0f, %.0f, %.0f), %.0f units from the ship, lifetime %.0fs",
              state_.cycle, zone[(int)site_.zone], pos_.x, pos_.y, pos_.z, world::bdetail::dist(pos_, ship), state_.lifeLeft);
        eng.events.emit(world::DistressBeaconSpawned{siteSeed_, pos_.x, pos_.y, pos_.z});
        if (auto* t = eng.services.get<core::IToast>()) t->show("DISTRESS SIGNAL DETECTED", core::IToast::Level::Warning, 6.0f);
    }

    void investigate(engine::Engine& eng) {
        world::claimBeacon(state_, params_);
        detected_ = false;
        gameplay::IInventory* inv = eng.services.get<gameplay::IInventory>();
        int box = 0, got = 0;
        if (inv) {
            box = inv->add(kBlackBox, 1);
            if (!oreId_.empty() && oreAmount_ > 0) got = inv->add(oreId_, oreAmount_);
        } else {
            LOG_W("distress_beacons", "no inventory: black box and %d %s not given", oreAmount_, oreId_.c_str());
        }
        LOG_I("distress_beacons", "DistressBeaconInvestigated: +%d black box, +%d %s (of %d); next beacon in %.0fs", box, got, oreId_.c_str(), oreAmount_,
              state_.untilSpawn);
        eng.events.emit(world::DistressBeaconInvestigated{oreId_, got});
        if (auto* t = eng.services.get<core::IToast>()) {
            std::string text = box > 0 ? "Distress beacon reached: BLACK BOX recovered" : "Distress beacon reached: black box lost (hold full)";
            if (!oreId_.empty() && oreAmount_ > 0) {
                std::string ore = oreId_;
                if (const core::IData* data = eng.services.get<core::IData>()) ore = data->get("ores", oreId_)["name"].str(oreId_);
                for (auto& ch : ore) ch = (char)std::toupper((unsigned char)ch);
                text += "  +" + std::to_string(got) + " " + ore;
                if (got < oreAmount_) text += " (hold full)";
            }
            t->show(text, core::IToast::Level::Info, 7.0f);
        }
    }

    world::IStarSystem* sys_ = nullptr;
    world::BeaconParams params_;
    world::BeaconState state_;
    world::BeaconWorld world_;
    world::BeaconSite site_;
    world::Vec3d pos_;
    uint32_t siteSeed_ = 0;
    std::string oreId_ = "cobalt";
    int oreAmount_ = 10;
    bool on_ = false, detected_ = false;
};

REGISTER_MODULE(DistressBeacons);
