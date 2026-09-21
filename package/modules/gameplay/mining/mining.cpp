// gameplay/mining - a destroyed asteroid (world::AsteroidDestroyed) leaves its ore: mining.mode = capsule (default: ONE flashing capsule, a magnet pulls it to
// the ship, collected on contact), instant (straight into the hold) or chunks (scattered chunks you must scoop slowly). Rules in mining_rules.h; docs/MINING.md.
#include <GL/gl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include "core/audio/audio_api.h"
#include "core/data_registry/data_api.h"
#include "core/render_engine/render_engine.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "fx/particles/particles_api.h"
#include "gameplay/inventory/inventory_api.h"
#include "gameplay/mining/mining_api.h"
#include "gameplay/mining/mining_rules.h"
#include "ship/ship_core/ship_api.h"
#include "world/asteroids/asteroids_api.h"

class Mining : public engine::Module {
public:
    const char* name() const override { return "gameplay/mining"; }
    std::vector<std::string> optionalDependencies() const override {
        return {"world/asteroids", "gameplay/inventory", "ship/ship_core", "ship/fake_ship", "fx/particles", "core/render_engine", "core/data_registry", "core/audio"};
    }

    bool init(engine::Engine& eng) override {
        auto& c = eng.config;
        if (!c.get("mining.enabled", true, "asteroid ore capsules / chunks and scooping")) return true;
        std::string mode = c.get<std::string>("mining.mode", "capsule", "what a destroyed asteroid leaves: capsule (one flashing capsule, magnet, collected on contact) | instant (straight into the hold) | chunks (scattered chunks, scoop slowly)");
        if (!gameplay::parseMode(mode, mode_)) { LOG_W("mining", "mining.mode '%s' is not capsule, instant or chunks: using capsule", mode.c_str()); mode_ = gameplay::Mode::Capsule; }
        yieldScale_ = std::max(0.0f, c.get("mining.yield_scale", 0.5f, "ore units per destroyed asteroid = this * radius^2 (radius 9: about 41)"));
        capsuleSeconds_ = std::max(1.0f, c.get("mining.capsule_seconds", 30.0f, "seconds a capsule lives (its flash speeds up in the last 5 s)"));
        magnetRadius_ = std::max(1.0f, c.get("mining.magnet_radius", 120.0f, "a capsule within this distance of the ship is pulled toward it, units"));
        magnetSpeed_ = std::max(10.0f, c.get("mining.magnet_speed", 140.0f, "fastest a capsule is pulled, m/s"));
        scoopRadius_ = std::max(0.5f, c.get("mining.scoop_radius", 20.0f, "a capsule this close to the ship is collected, units"));
        scoopSpeed_ = std::max(0.0f, c.get("mining.scoop_speed", 0.0f, "highest speed relative to a capsule at which it can be collected, m/s (0 = no limit)"));
        range_ = std::max(10.0f, c.get("mining.chunk_range", 3000.0f, "chunks farther than this from the ship disappear; a rock destroyed farther away than this goes straight into the hold, units"));
        countScale_ = std::max(0.0f, c.get("mining.chunk_count_scale", 1.0f, "chunks mode: multiplier on the number of chunks a rock breaks into"));
        lifetime_ = std::max(1.0f, c.get("mining.chunk_lifetime", 300.0f, "chunks mode: seconds before a chunk disappears"));
        chunkScoopRadius_ = std::max(0.5f, c.get("mining.chunk_scoop_radius", 12.0f, "chunks mode: how close the ship must be to scoop a chunk, units"));
        chunkScoopSpeed_ = std::max(0.0f, c.get("mining.chunk_scoop_speed", 40.0f, "chunks mode: highest speed relative to the chunk at which it can be scooped, m/s"));
        int cap = std::clamp(c.get("mining.max_chunks", 96, "most ore capsules (or chunks) alive at once"), 0, 4096);
        // ore table: ids and colours from data/ores.json ("rock" is always index 0)
        oreIds_ = {"rock"}; oreCol_ = {0.5f, 0.47f, 0.42f};
        if (auto* data = eng.services.get<core::IData>()) {
            for (auto& id : data->ids("ores")) {
                if (id == "rock") continue;
                oreIds_.push_back(id);
                const engine::Json& o = data->get("ores", id);
                for (int k = 0; k < 3; k++) oreCol_.push_back((float)o["color"].at(k).num(0.6));
            }
        }
        pool_.init(cap);
        capsules_.init(cap);
        quads_.assign((size_t)std::max(1, cap) * 12 * 3, 0); quadCol_.assign((size_t)std::max(1, cap) * 12 * 3, 0);     // chunks: 4 vertices, capsules: 8
        rng_ = gameplay::Rng(0x0e5eed11u);
        eng_ = &eng;
        eng.events.subscribe<world::AsteroidDestroyed>([this](const world::AsteroidDestroyed& e) { onDestroyed(e); });
        if (auto* r = eng.services.get<core::RenderEngine>()) {
            render_ = r;
            r->addPass("mining/chunks", 66, [this](core::RenderEngine& rr) { draw(rr); });   // after the asteroids (60) and stations (65), before the particles (70)
        }
        active_ = true;
        LOG_I("mining", "mode %s, yield %.2f x radius^2, %d capsules/chunks max; capsule %.0f s, magnet %.0f units, collect within %.0f units%s", mode.c_str(), yieldScale_, cap,
              capsuleSeconds_, magnetRadius_, scoopRadius_, scoopSpeed_ > 0 ? "" : " at any speed");
        return true;
    }

    void shutdown(engine::Engine&) override { if (active_ && render_) render_->removePass("mining/chunks"); }

    void onFixedUpdate(engine::Engine& eng, float dt) override {
        if (!active_ || (pool_.n == 0 && capsules_.n == 0)) return;
        auto t0 = std::chrono::steady_clock::now();
        auto* ship = eng.services.get<ship::IShip>();
        gameplay::Vec3d ref{}, sv{};
        bool have = ship != nullptr;
        if (ship) { auto p = ship->position(); auto v = ship->velocity(); ref = {p.x, p.y, p.z}; sv = {v.x, v.y, v.z}; }
        else if (pool_.n > 0) ref = {pool_.px[0], pool_.py[0], pool_.pz[0]};              // no ship: chunks just drift (nothing to scoop them)
        gameplay::advanceChunks(pool_, dt, lifetime_, range_, ref);
        if (have && ship->status().alive) scoopChunks(eng, ref, sv);
        stepCapsules(eng, dt, ref, sv, have && ship->status().alive);
        cpuMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        steps_++;
        if (eng.time() - lastReport_ >= 5.0) {
            lastReport_ = eng.time();
            LOG_D("mining", "%d capsules, %d chunks alive, CPU per step %.4f ms", capsules_.n, pool_.n, steps_ ? cpuMs_ / steps_ : 0.0);
            cpuMs_ = 0; steps_ = 0;
        }
    }

private:
    uint16_t oreIndex(const std::string& id) const {
        for (size_t i = 0; i < oreIds_.size(); i++) if (oreIds_[i] == id) return (uint16_t)i;
        return 0;                                                                       // unknown ore: worthless "rock"
    }

    void pickedUp(engine::Engine& eng, const std::string& id, int got, const gameplay::Vec3d& at, uint16_t oi, gameplay::IInventory* inv) {
        eng.events.emit(gameplay::OreMined{id, got});
        LOG_I("mining", "collected %d %s (%s hold %.0f / %.0f, cargo total %.0f / %.0f)", got, id.c_str(), id.c_str(), inv->capacity(id) - inv->free(id), inv->capacity(id), inv->used(), inv->capacity());
        fx::SpawnParticles s; s.kind = "spark"; s.position = at; s.count = 4; s.size = 0.5f;
        for (int k = 0; k < 3; k++) s.colour[k] = oreCol_[oi * 3 + k];
        eng.events.emit(s);
        if (auto* audio = eng.services.get<core::IAudio>()) if (audio->hasSound("pickup")) audio->play("pickup", 0.5f);
    }

    void onDestroyed(const world::AsteroidDestroyed& e) {
        if (!active_) return;
        int total = gameplay::totalYield(e.radius, yieldScale_);
        uint16_t oi = oreIndex(e.ore);
        if (mode_ == gameplay::Mode::Chunks) {
            int n = gameplay::chunkCount(total, countScale_);
            gameplay::splitAmount(total, n, parts_);
            for (int amount : parts_) pool_.spawn(e.position, gameplay::chunkVelocity(rng_, 2.0f, 10.0f), amount, oi);
            LOG_I("mining", "asteroid %d (%s, radius %.1f) broke into %zu chunks holding %d ore units", e.id, oreIds_[oi].c_str(), e.radius, parts_.size(), total);
            return;
        }
        auto* ship = eng_->services.get<ship::IShip>();
        auto* inv = eng_->services.get<gameplay::IInventory>();
        double dist = 0;
        if (ship) { auto p = ship->position(); double dx = e.position.x - p.x, dy = e.position.y - p.y, dz = e.position.z - p.z; dist = std::sqrt(dx * dx + dy * dy + dz * dz); }
        int remaining = total;
        if (inv && gameplay::goesStraightToHold(mode_, dist, range_)) {
            int got = inv->add(oreIds_[oi], total);
            if (got > 0) pickedUp(*eng_, oreIds_[oi], got, e.position, oi, inv);
            remaining = total - got;                                                    // what did not fit waits in a (red) capsule
            LOG_I("mining", "asteroid %d (%s, radius %.1f): %d ore units %s", e.id, oreIds_[oi].c_str(), e.radius, got, mode_ == gameplay::Mode::Instant ? "added at once" : "were too far away to wait for: added at once");
            if (remaining > 0) capsules_.spawn(e.position, remaining, oi, capsuleSeconds_, true);
            return;
        }
        capsules_.spawn(e.position, remaining, oi, capsuleSeconds_, false);
        LOG_I("mining", "asteroid %d (%s, radius %.1f) left a capsule with %d ore units (%.0f s)", e.id, oreIds_[oi].c_str(), e.radius, total, capsuleSeconds_);
    }

    // ---- capsules ----
    void stepCapsules(engine::Engine& eng, float dt, const gameplay::Vec3d& ship, const gameplay::Vec3d& sv, bool alive) {
        if (capsules_.n == 0) return;
        auto* inv = eng.services.get<gameplay::IInventory>();
        for (int i = capsules_.n - 1; i >= 0; i--) {
            if (alive) gameplay::magnetStep(capsules_, i, ship, dt, magnetRadius_, magnetSpeed_);
            capsules_.age[i] += dt;
            if (capsules_.age[i] >= capsules_.life[i]) { LOG_I("mining", "a capsule with %d %s expired", capsules_.amount[i], oreIds_[capsules_.ore[i]].c_str()); capsules_.remove(i); continue; }
            if (!alive || !inv) continue;
            double dx = capsules_.px[i] - ship.x, dy = capsules_.py[i] - ship.y, dz = capsules_.pz[i] - ship.z, d = std::sqrt(dx * dx + dy * dy + dz * dz);
            double rel = std::sqrt((sv.x - capsules_.vx[i]) * (sv.x - capsules_.vx[i]) + (sv.y - capsules_.vy[i]) * (sv.y - capsules_.vy[i]) + (sv.z - capsules_.vz[i]) * (sv.z - capsules_.vz[i]));
            if (!gameplay::canScoopAny(d, rel, scoopRadius_, scoopSpeed_)) continue;
            const std::string& id = oreIds_[capsules_.ore[i]];
            if (capsules_.blocked[i] && inv->free(id) < 1.0f) continue;                  // still full: stays, red, until it expires (or room appears)
            int got = inv->add(id, capsules_.amount[i]);                                 // partial when the hold is nearly full; the inventory emits CargoFull
            gameplay::PickupOutcome o = gameplay::afterPickup(capsules_.amount[i], got);
            if (got > 0) pickedUp(eng, id, got, {capsules_.px[i], capsules_.py[i], capsules_.pz[i]}, capsules_.ore[i], inv);
            if (o.removeCapsule) capsules_.remove(i);
            else {
                capsules_.amount[i] = o.remaining; capsules_.blocked[i] = 1;
                LOG_I("mining", "%s hold full: %d %s left in the capsule (flashing red)", id.c_str(), o.remaining, id.c_str());
            }
        }
    }

    // ---- chunks (mining.mode = chunks) ----
    void scoopChunks(engine::Engine& eng, const gameplay::Vec3d& ship, const gameplay::Vec3d& sv) {
        if (pool_.n == 0) return;
        auto* inv = eng.services.get<gameplay::IInventory>();
        if (!inv) return;
        for (int i = pool_.n - 1; i >= 0; i--) {
            double dx = pool_.px[i] - ship.x, dy = pool_.py[i] - ship.y, dz = pool_.pz[i] - ship.z, d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d > chunkScoopRadius_) { pool_.blocked[i] = 0; continue; }
            if (pool_.blocked[i]) continue;                                             // the hold said no: try again after leaving and coming back
            double rvx = sv.x - pool_.vx[i], rvy = sv.y - pool_.vy[i], rvz = sv.z - pool_.vz[i], rel = std::sqrt(rvx * rvx + rvy * rvy + rvz * rvz);
            if (!gameplay::canScoop(d, rel, chunkScoopRadius_, chunkScoopSpeed_)) continue;   // too fast: it flies by
            const std::string& id = oreIds_[pool_.ore[i]];
            int got = inv->add(id, pool_.amount[i]);
            if (got > 0) pickedUp(eng, id, got, {pool_.px[i], pool_.py[i], pool_.pz[i]}, pool_.ore[i], inv);
            if (got >= pool_.amount[i]) pool_.remove(i);
            else { pool_.amount[i] -= got; pool_.blocked[i] = 1; LOG_I("mining", "%s hold full: %d %s left in the chunk", id.c_str(), pool_.amount[i], id.c_str()); }
        }
    }

    // one glDrawArrays(GL_QUADS) for everything: chunks are camera-facing squares, capsules two flashing diamonds (the ore colour, or red when full, around a white core)
    void draw(core::RenderEngine& r) {
        if (pool_.n == 0 && capsules_.n == 0) return;
        float m[16];
        for (int i = 0; i < 16; i++) m[i] = r.camera.view[i];
        gameplay::Vec3d cam{-((double)m[0] * m[12] + (double)m[1] * m[13] + (double)m[2] * m[14]),
                            -((double)m[4] * m[12] + (double)m[5] * m[13] + (double)m[6] * m[14]),
                            -((double)m[8] * m[12] + (double)m[9] * m[13] + (double)m[10] * m[14])};
        const float rx = m[0], ry = m[4], rz = m[8], ux = m[1], uy = m[5], uz = m[9];
        const double fx = -m[2], fy = -m[6], fz = -m[10];
        m[12] = m[13] = m[14] = 0;
        glLoadMatrixf(m);
        int verts = 0;
        const int maxVerts = (int)(quads_.size() / 3);
        auto quad = [&](float cx, float cy, float cz, float h, bool diamond, float cr, float cg, float cb) {
            if (verts + 4 > maxVerts) return;
            float* q = &quads_[(size_t)verts * 3]; float* col = &quadCol_[(size_t)verts * 3];
            const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
            const float dx4[4] = {-1, 0, 1, 0}, dy4[4] = {0, -1, 0, 1};
            for (int k = 0; k < 4; k++) {
                float a = diamond ? dx4[k] : sx[k], b = diamond ? dy4[k] : sy[k];
                q[k * 3] = cx + (rx * a + ux * b) * h; q[k * 3 + 1] = cy + (ry * a + uy * b) * h; q[k * 3 + 2] = cz + (rz * a + uz * b) * h;
                col[k * 3] = cr; col[k * 3 + 1] = cg; col[k * 3 + 2] = cb;
            }
            verts += 4;
        };
        for (int i = 0; i < pool_.n; i++) {
            double dx = pool_.px[i] - cam.x, dy = pool_.py[i] - cam.y, dz = pool_.pz[i] - cam.z, dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dx * fx + dy * fy + dz * fz <= 0.5) continue;
            float h = (float)std::max(0.25 + 0.06 * std::sqrt((double)pool_.amount[i]), dist * 0.0045);
            const float* c = &oreCol_[pool_.ore[i] * 3];
            quad((float)dx, (float)dy, (float)dz, h, false, std::min(1.0f, c[0] * 1.2f + 0.15f), std::min(1.0f, c[1] * 1.2f + 0.15f), std::min(1.0f, c[2] * 1.2f + 0.15f));
        }
        for (int i = 0; i < capsules_.n; i++) {
            double dx = capsules_.px[i] - cam.x, dy = capsules_.py[i] - cam.y, dz = capsules_.pz[i] - cam.z, dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dx * fx + dy * fy + dz * fz <= 0.5) continue;
            gameplay::CapsuleLook look = gameplay::capsuleLook(capsules_.age[i], capsules_.life[i], capsules_.blocked[i] != 0);
            float base = (float)std::max(1.6, dist * 0.0075);                            // a template diamond, never smaller than ~6 pixels
            const float* c = &oreCol_[capsules_.ore[i] * 3];
            float br = 0.55f + 0.45f * look.pulse;                                       // dim <-> bright with the flash
            float r0 = look.red ? 1.0f : c[0], g0 = look.red ? 0.12f : c[1], b0 = look.red ? 0.08f : c[2];
            float lift = look.red ? 0.0f : 0.25f;
            quad((float)dx, (float)dy, (float)dz, base * look.size, true, std::min(1.0f, (r0 + lift) * br), std::min(1.0f, (g0 + lift) * br), std::min(1.0f, (b0 + lift) * br));
            float w = look.white;
            quad((float)dx, (float)dy, (float)dz, base * look.size * 0.5f, true, w, w, w);   // the white core
        }
        if (verts > 0) {
            glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
            glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
            glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
            glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
            glVertexPointer(3, GL_FLOAT, 0, quads_.data());
            glColorPointer(3, GL_FLOAT, 0, quadCol_.data());
            glDrawArrays(GL_QUADS, 0, verts);
            glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_VERTEX_ARRAY);
            glPopAttrib();
        }
    }

    engine::Engine* eng_ = nullptr;
    core::RenderEngine* render_ = nullptr;
    bool active_ = false;
    gameplay::Mode mode_ = gameplay::Mode::Capsule;
    float yieldScale_ = 0.5f, capsuleSeconds_ = 30.0f, magnetRadius_ = 120.0f, magnetSpeed_ = 140.0f, scoopRadius_ = 20.0f, scoopSpeed_ = 0.0f;
    float countScale_ = 1.0f, lifetime_ = 300.0f, range_ = 3000.0f, chunkScoopRadius_ = 12.0f, chunkScoopSpeed_ = 40.0f;
    double cpuMs_ = 0, lastReport_ = -10;
    long steps_ = 0;
    gameplay::ChunkPool pool_;
    gameplay::CapsulePool capsules_;
    gameplay::Rng rng_;
    std::vector<std::string> oreIds_;
    std::vector<float> oreCol_, quads_, quadCol_;
    std::vector<int> parts_;
};

REGISTER_MODULE(Mining);
