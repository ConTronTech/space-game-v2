// world/cargo_pods - ambient loot: a small pool of drifting cargo pods scattered through the system (deep space / near a body / in the belt -
// the distress-beacon zones), each slot on its own seeded spawn / respawn timer, always drawn (a small amber crate marker, no perk). Fly
// within the magnet radius and the pod is pulled in and collected on contact, exactly like a mining capsule (it reads the SAME
// mining.magnet_radius / magnet_speed / scoop_radius / scoop_speed keys). Each pod holds a few units of a weighted ore and sometimes a crafted
// item. Provides world::ICargoPods. Rules in pod_rules.h; design in docs/CARGO_PODS.md. No save: a reload re-rolls every slot from the seed.
#include <GL/gl.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include "core/audio/audio_api.h"
#include "core/data_registry/data_api.h"
#include "core/render_engine/render_engine.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "fx/particles/particles_api.h"
#include "gameplay/inventory/inventory_api.h"
#include "gameplay/mining/mining_api.h"
#include "ship/ship_core/ship_api.h"
#include "ui/toast/toast_api.h"
#include "world/asteroids/asteroids_api.h"
#include "world/cargo_pods/cargo_pods_api.h"
#include "world/cargo_pods/pod_rules.h"
#include "world/star_system/star_system_api.h"
#include "world/stations/stations_api.h"

namespace {
constexpr int kBeltSample = 64;      // asteroid positions offered to the site pick (evenly spaced indices: deterministic)
constexpr float kPodColour[3] = {1.0f, 0.62f, 0.18f};   // amber crate (distinct from the ore-coloured mining capsules)

struct Pod {
    world::PodSite site;
    world::Vec3d pos, vel, drift;    // vel / drift are relative to the site's frame (its anchor body, or fixed space)
    int ore = -1, amount = 0, item = -1;
    bool blocked = false;            // the hold refused part of it: stops, flashes red, retried once there is room
};

std::string upperName(const core::IData* data, const char* cat, const std::string& id) {
    std::string s = data ? data->get(cat, id)["name"].str(id) : id;
    for (auto& ch : s) ch = (char)std::toupper((unsigned char)ch);
    return s;
}
} // namespace

class CargoPods : public engine::Module, public world::ICargoPods {
public:
    const char* name() const override { return "world/cargo_pods"; }
    std::vector<std::string> dependencies() const override { return {"world/star_system"}; }
    std::vector<std::string> optionalDependencies() const override {
        return {"core/data_registry", "core/render_engine", "core/audio", "fx/particles", "gameplay/inventory", "ui/toast", "ship/ship_core",
                "world/asteroids", "world/stations"};
    }

    bool init(engine::Engine& eng) override {
        auto& c = eng.config;
        if (!c.get("cargo_pods.enabled", true, "a small pool of drifting cargo pods with a little ore to scoop up (docs/CARGO_PODS.md)")) return true;
        sys_ = eng.services.get<world::IStarSystem>();
        if (!sys_) { LOG_W("cargo_pods", "no star system: no cargo pods"); return true; }

        world::PodParams p;
        p.poolSize = c.get("cargo_pods.pool_size", 6, "pods that can exist at once (each slot has its own timer)");
        p.respawnMin = c.get("cargo_pods.respawn_min", 60.0f, "seconds: shortest wait before an emptied slot spawns a new pod (the first fill at boot is within this)");
        p.respawnMax = c.get("cargo_pods.respawn_max", 240.0f, "seconds: longest wait (each wait is a seeded roll in [min, max])");
        p.lifetime = c.get("cargo_pods.lifetime_seconds", 900.0f, "seconds an unclaimed pod lasts before its slot re-rolls it elsewhere (0 = until collected)");
        p.oreMin = c.get("cargo_pods.ore_reward_min", 2, "fewest ore units in a pod (ore type: a weighted pick, data/ores.json rarity)");
        p.oreMax = c.get("cargo_pods.ore_reward_max", 6, "most ore units in a pod");
        p.itemChance = c.get("cargo_pods.item_chance", 0.15f, "0..1: chance a pod also holds one crafted item from cargo_pods.item_ids");
        p.drift = c.get("cargo_pods.drift_speed", 2.0f, "m/s: fastest a pod's slow seeded drift");
        // pickup: the SAME keys (and defaults) as gameplay/mining's capsules - one feel for everything the magnet pulls in
        p.magnetRadius = c.get("mining.magnet_radius", 120.0f, "a capsule within this distance of the ship is pulled toward it, units");
        p.magnetSpeed = c.get("mining.magnet_speed", 140.0f, "fastest a capsule is pulled, m/s");
        p.scoopRadius = c.get("mining.scoop_radius", 20.0f, "a capsule this close to the ship is collected, units");
        p.scoopSpeed = c.get("mining.scoop_speed", 0.0f, "highest speed relative to a capsule at which it can be collected, m/s (0 = no limit)");
        params_ = world::sanitizePodParams(p);
        if (params_.poolSize <= 0) { LOG_I("cargo_pods", "pool_size 0: no cargo pods"); return true; }
        const std::string items = c.get("cargo_pods.item_ids", std::string("repair_kit,missile_pack"), "comma-separated item ids (data/items.json) a pod's bonus item is picked from, \"\" = never");

        // reward tables: ores weighted by rarity; items checked against data/items.json when it is there
        const core::IData* data = eng.services.get<core::IData>();
        data_ = data;
        if (data)
            for (const auto& id : data->ids("ores")) {
                if (id == "rock") continue;
                oreIds_.push_back(id);
                oreWeights_.push_back((float)data->get("ores", id)["rarity"].num(1.0));
            }
        if (oreIds_.empty()) { oreIds_ = {"iron"}; oreWeights_ = {1.0f}; }
        for (size_t a = 0; a <= items.size();) {
            size_t b = items.find(',', a);
            if (b == std::string::npos) b = items.size();
            std::string id = items.substr(a, b - a);
            id.erase(0, id.find_first_not_of(" \t"));
            id.erase(id.find_last_not_of(" \t") + 1);
            if (!id.empty()) {
                if (data && data->get("items", id).isNull()) LOG_W("cargo_pods", "cargo_pods.item_ids: '%s' is not in data/items.json: skipped", id.c_str());
                else itemIds_.push_back(id);
            }
            a = b + 1;
        }

        world_.sun = sys_->sunPosition();
        for (const world::Body& b : sys_->bodies()) world_.bodies.push_back({b.id, b.kind, b.radius, b.orbitRadius, b.parent});
        if (const world::IStations* st = eng.services.get<world::IStations>())
            for (int i = 0; i < st->count(); i++) {
                world::StationInfo in = st->info(i);
                world_.stations.push_back({in.parent, world::pdetail::dist(in.position, sys_->positionAt(in.parent))});
            }
        if (const world::IAsteroids* ast = eng.services.get<world::IAsteroids>()) {
            const int n = ast->count();
            for (int k = 0; k < kBeltSample && n > 0; k++) world_.belt.push_back(ast->position((int)((long long)k * n / kBeltSample)));
        }

        const unsigned seed = (unsigned)c.get("world.seed", 1234.0f, "seed of the star system (same seed = same system)");
        slots_ = world::startPodPool(seed ^ 0xCA260D5u, params_);
        pods_.assign(slots_.size(), Pod{});
        if (eng.hasFlag("cargo-pods-now")) {   // test flag: every slot spawns within the first few seconds
            for (size_t i = 0; i < slots_.size(); i++) slots_[i].untilSpawn = 1.0 + 0.25 * (double)i;
            LOG_I("cargo_pods", "--cargo-pods-now: every slot spawns within %.1fs", 1.0 + 0.25 * (double)slots_.size());
        }
        verts_.assign(slots_.size() * 8 * 3, 0.0f);
        cols_.assign(slots_.size() * 8 * 3, 0.0f);
        if (auto* r = eng.services.get<core::RenderEngine>()) {
            render_ = r;
            r->addPass("world/cargo_pods", 67, [this](core::RenderEngine& rr) { draw(rr); });   // after the mining capsules (66), before the particles (70)
        }
        eng.services.provide<world::ICargoPods>(this);
        on_ = true;
        LOG_I("cargo_pods", "%zu slots, respawn %.0f-%.0fs, lifetime %s, %d-%d ore (%zu ore types by rarity), item %.0f%% of %zu, magnet %.0f / scoop %.0f units (mining.*)",
              slots_.size(), params_.respawnMin, params_.respawnMax, params_.lifetime > 0 ? (std::to_string((int)params_.lifetime) + "s").c_str() : "until collected",
              params_.oreMin, params_.oreMax, oreIds_.size(), params_.itemChance * 100.0, itemIds_.size(), params_.magnetRadius, params_.scoopRadius);
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        if (on_) {
            eng.services.withdraw<world::ICargoPods>();
            if (render_) render_->removePass("world/cargo_pods");
        }
        on_ = false;
    }

    // Fixed step: frozen while paused, like the rest of the simulation.
    void onFixedUpdate(engine::Engine& eng, float dt) override {
        if (!on_ || !(dt > 0)) return;
        time_ += dt;
        const ship::IShip* ship = eng.services.get<ship::IShip>();
        const bool alive = ship && ship->status().alive;
        const engine::Vec3d sp = ship ? ship->positionD() : engine::Vec3d{world_.sun.x, world_.sun.y, world_.sun.z};
        const world::Vec3d shipPos{sp.x, sp.y, sp.z};
        world::Vec3d shipVel{};
        if (ship) { engine::Vec3 v = ship->velocity(); shipVel = {v.x, v.y, v.z}; }
        gameplay::IInventory* inv = eng.services.get<gameplay::IInventory>();
        for (size_t i = 0; i < slots_.size(); i++) {
            const world::PodStep st = world::stepPodSlot(slots_[i], params_, dt);
            if (st.spawned) spawn(eng, (int)i, shipPos);
            if (st.expired) {
                LOG_I("cargo_pods", "pod %zu drifted off unclaimed; slot respawns in %.0fs", i, slots_[i].untilSpawn);
                pods_[i] = Pod{};
                continue;
            }
            if (!slots_[i].active) continue;
            Pod& pod = pods_[i];
            const world::Vec3d anchor = pod.site.anchor >= 0 ? sys_->positionAt(pod.site.anchor) : world::Vec3d{};
            pod.pos = world::podPosition(pod.site, anchor);
            pod.vel = world::podMagnetVelocity(pod.vel, pod.drift, pod.pos, shipPos, dt, alive ? params_.magnetRadius : 0.0, params_.magnetSpeed, pod.blocked);
            pod.site.offset = world::pdetail::add(pod.site.offset, world::pdetail::mul(pod.vel, dt));
            pod.pos = world::podPosition(pod.site, anchor);
            if (!alive || !inv) continue;
            // relative speed ignores an anchor body's own orbital motion (only matters if mining.scoop_speed > 0; the default 0 = any speed)
            const double d = world::pdetail::dist(pod.pos, shipPos), rel = world::pdetail::dist(pod.vel, shipVel);
            if (!world::podCanScoop(d, rel, params_.scoopRadius, params_.scoopSpeed)) continue;
            pickUp(eng, (int)i, *inv);
        }
    }

    // ---- world::ICargoPods ----
    int count() const override { return on_ ? (int)slots_.size() : 0; }
    int activeCount() const override { return on_ ? world::activePods(slots_) : 0; }
    bool alive(int i) const override { return on_ && i >= 0 && i < (int)slots_.size() && slots_[(size_t)i].active; }
    world::Vec3d position(int i) const override { return i >= 0 && i < (int)pods_.size() ? pods_[(size_t)i].pos : world::Vec3d{}; }

private:
    void spawn(engine::Engine& eng, int i, const world::Vec3d& ship) {
        const uint32_t siteSeed = world::podSiteSeed(slots_[(size_t)i].seed, slots_[(size_t)i].cycle);
        const std::vector<world::PodSite> cands = world::podCandidates(world_, siteSeed);
        std::vector<world::Vec3d> where;
        for (const auto& s : cands) where.push_back(world::podPosition(s, s.anchor >= 0 ? sys_->positionAt(s.anchor) : world::Vec3d{}));
        const int pick = std::max(0, world::pickPodCandidate(where, ship, params_.magnetRadius * 2.0, siteSeed));
        Pod& pod = pods_[(size_t)i];
        pod = Pod{};
        pod.site = cands[(size_t)pick];
        pod.pos = where[(size_t)pick];
        pod.drift = world::podDrift(siteSeed, params_.drift);
        pod.vel = pod.drift;
        const world::PodReward r = world::rollPodReward(siteSeed, oreWeights_, params_.oreMin, params_.oreMax, params_.itemChance, (int)itemIds_.size());
        pod.ore = r.ore; pod.amount = r.amount; pod.item = r.item;
        static const char* zone[] = {"deep space", "near body", "belt"};
        LOG_I("cargo_pods", "CargoPodSpawned slot %d (%s) at (%.0f, %.0f, %.0f), %.0f units from the ship: %d %s%s%s", i, zone[(int)pod.site.zone],
              pod.pos.x, pod.pos.y, pod.pos.z, world::pdetail::dist(pod.pos, ship), pod.amount, pod.ore >= 0 ? oreIds_[(size_t)pod.ore].c_str() : "-",
              pod.item >= 0 ? " + " : "", pod.item >= 0 ? itemIds_[(size_t)pod.item].c_str() : "");
        eng.events.emit(world::CargoPodSpawned{i, pod.pos.x, pod.pos.y, pod.pos.z});
    }

    // The scoop touched pod i: as much as fits goes in; the rest stays (red, still) and is retried once the hold has room for it.
    void pickUp(engine::Engine& eng, int i, gameplay::IInventory& inv) {
        Pod& pod = pods_[(size_t)i];
        const std::string ore = pod.ore >= 0 && pod.amount > 0 ? oreIds_[(size_t)pod.ore] : std::string();
        const std::string item = pod.item >= 0 ? itemIds_[(size_t)pod.item] : std::string();
        if (pod.blocked && !(!ore.empty() && inv.free(ore) >= 1.0f) && !(!item.empty() && inv.free(item) >= 1.0f)) return;   // still full
        int got = 0, gotItem = 0;
        if (!ore.empty() && (!pod.blocked || inv.free(ore) >= 1.0f)) got = inv.add(ore, pod.amount);
        if (!item.empty() && (!pod.blocked || inv.free(item) >= 1.0f)) gotItem = inv.add(item, 1);
        pod.amount -= got;
        if (gotItem > 0) pod.item = -1;
        if (got > 0 || gotItem > 0) {
            eng.events.emit(world::CargoPodCollected{i, got > 0 ? ore : std::string(), got, gotItem > 0 ? item : std::string()});
            if (got > 0) eng.events.emit(gameplay::OreMined{ore, got});   // the ship HUD's "+N ORE" pickup banner
            fx::SpawnParticles s; s.kind = "spark"; s.position = pod.pos; s.count = 6; s.size = 0.6f;
            for (int k = 0; k < 3; k++) s.colour[k] = kPodColour[k];
            eng.events.emit(s);
            if (auto* audio = eng.services.get<core::IAudio>()) if (audio->hasSound("pickup")) audio->play("pickup", 0.5f);
            if (gotItem > 0)   // the only toast: a rare bonus item (ore pickups already get the HUD banner; spawns / expiries are log-only)
                if (auto* t = eng.services.get<core::IToast>()) t->show("Cargo pod: " + upperName(data_, "items", item) + " recovered", core::IToast::Level::Info, 4.0f);
        }
        const bool left = pod.amount > 0 || pod.item >= 0;
        LOG_I("cargo_pods", "CargoPodCollected slot %d: +%d %s%s%s%s", i, got, ore.empty() ? "-" : ore.c_str(), gotItem > 0 ? " + " : "",
              gotItem > 0 ? item.c_str() : "", left ? " (hold full: the rest waits in the pod)" : "");
        if (left) { pod.blocked = true; pod.vel = {}; return; }
        world::collectPod(slots_[(size_t)i], params_);
        pods_[(size_t)i] = Pod{};
    }

    // One glDrawArrays(GL_QUADS): each pod is a camera-facing amber square (a crate) around a pulsing white diamond core, never smaller than
    // a few pixels (the mining capsule's billboard, recoloured). Red when the hold refused it. Beyond the camera's far plane it is clipped.
    void draw(core::RenderEngine& r) {
        if (world::activePods(slots_) == 0) return;
        float m[16];
        for (int i = 0; i < 16; i++) m[i] = r.camera.view[i];
        const world::Vec3d cam{-((double)m[0] * m[12] + (double)m[1] * m[13] + (double)m[2] * m[14]),
                               -((double)m[4] * m[12] + (double)m[5] * m[13] + (double)m[6] * m[14]),
                               -((double)m[8] * m[12] + (double)m[9] * m[13] + (double)m[10] * m[14])};
        const float rx = m[0], ry = m[4], rz = m[8], ux = m[1], uy = m[5], uz = m[9];
        const double fx = -m[2], fy = -m[6], fz = -m[10];
        m[12] = m[13] = m[14] = 0;
        int verts = 0;
        const int maxVerts = (int)(verts_.size() / 3);
        auto quad = [&](float cx, float cy, float cz, float h, bool diamond, float cr, float cg, float cb) {
            if (verts + 4 > maxVerts) return;
            float* q = &verts_[(size_t)verts * 3];
            float* col = &cols_[(size_t)verts * 3];
            const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1}, dx4[4] = {-1, 0, 1, 0}, dy4[4] = {0, -1, 0, 1};
            for (int k = 0; k < 4; k++) {
                const float a = diamond ? dx4[k] : sx[k], b = diamond ? dy4[k] : sy[k];
                q[k * 3] = cx + (rx * a + ux * b) * h; q[k * 3 + 1] = cy + (ry * a + uy * b) * h; q[k * 3 + 2] = cz + (rz * a + uz * b) * h;
                col[k * 3] = cr; col[k * 3 + 1] = cg; col[k * 3 + 2] = cb;
            }
            verts += 4;
        };
        for (size_t i = 0; i < slots_.size(); i++) {
            if (!slots_[i].active) continue;
            const Pod& pod = pods_[i];
            const double dx = pod.pos.x - cam.x, dy = pod.pos.y - cam.y, dz = pod.pos.z - cam.z, dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dx * fx + dy * fy + dz * fz <= 0.5) continue;
            const float pulse = 0.5f + 0.5f * (float)std::sin(6.28318530718 * (0.6 * time_ + 0.37 * (double)i));   // slow, per-slot phase
            const float h = (float)std::max(2.0, dist * 0.0065);
            const float br = 0.7f + 0.3f * pulse;
            const float c0 = pod.blocked ? 1.0f : kPodColour[0], c1 = pod.blocked ? 0.12f : kPodColour[1], c2 = pod.blocked ? 0.08f : kPodColour[2];
            quad((float)dx, (float)dy, (float)dz, h, false, c0 * br, c1 * br, c2 * br);
            const float w = 0.45f + 0.55f * pulse;
            quad((float)dx, (float)dy, (float)dz, h * 0.6f, true, w, w, w);
        }
        if (verts == 0) return;
        glLoadMatrixf(m);
        glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
        glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
        glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, verts_.data());
        glColorPointer(3, GL_FLOAT, 0, cols_.data());
        glDrawArrays(GL_QUADS, 0, verts);
        glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_VERTEX_ARRAY);
        glPopAttrib();
    }

    world::IStarSystem* sys_ = nullptr;
    core::RenderEngine* render_ = nullptr;
    const core::IData* data_ = nullptr;
    world::PodParams params_;
    world::PodWorld world_;
    std::vector<world::PodSlot> slots_;
    std::vector<Pod> pods_;
    std::vector<std::string> oreIds_, itemIds_;
    std::vector<float> oreWeights_, verts_, cols_;
    double time_ = 0;
    bool on_ = false;
};

REGISTER_MODULE(CargoPods);
