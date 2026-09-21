// gameplay/mining - a destroyed asteroid (world::AsteroidDestroyed) breaks into ore chunks that drift on; slow down next to them to scoop them into the hold.
// Rules in mining_rules.h; yield, chunks, scoop rules and tunables in docs/MINING.md.
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
        if (!c.get("mining.enabled", true, "asteroid ore chunks and scooping")) return true;
        yieldScale_ = std::max(0.0f, c.get("mining.yield_scale", 0.5f, "ore units per destroyed asteroid = this * radius^2 (radius 9: about 41)"));
        countScale_ = std::max(0.0f, c.get("mining.chunk_count_scale", 1.0f, "multiplier on the number of chunks a rock breaks into"));
        lifetime_ = std::max(1.0f, c.get("mining.chunk_lifetime", 300.0f, "seconds before a chunk disappears"));
        range_ = std::max(10.0f, c.get("mining.chunk_range", 3000.0f, "chunks farther than this from the ship disappear, units"));
        scoopRadius_ = std::max(0.5f, c.get("mining.scoop_radius", 12.0f, "how close the ship must be to scoop a chunk, units"));
        scoopSpeed_ = std::max(0.0f, c.get("mining.scoop_speed", 40.0f, "highest speed relative to the chunk at which it can be scooped, m/s"));
        int cap = std::clamp(c.get("mining.max_chunks", 96, "most ore chunks alive at once (the oldest is dropped)"), 0, 4096);
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
        quads_.assign((size_t)std::max(1, cap) * 4 * 3, 0); quadCol_.assign((size_t)std::max(1, cap) * 4 * 3, 0);
        rng_ = gameplay::Rng(0x0e5eed11u);
        eng_ = &eng;
        eng.events.subscribe<world::AsteroidDestroyed>([this](const world::AsteroidDestroyed& e) { onDestroyed(e); });
        if (auto* r = eng.services.get<core::RenderEngine>()) {
            render_ = r;
            r->addPass("mining/chunks", 66, [this](core::RenderEngine& rr) { draw(rr); });   // after the asteroids (60) and stations (65), before the particles (70)
        }
        active_ = true;
        LOG_I("mining", "yield %.2f x radius^2, %d chunks max, scoop %.0f units at <= %.0f m/s", yieldScale_, cap, scoopRadius_, scoopSpeed_);
        return true;
    }

    void shutdown(engine::Engine&) override { if (active_ && render_) render_->removePass("mining/chunks"); }

    void onFixedUpdate(engine::Engine& eng, float dt) override {
        if (!active_ || pool_.n == 0) return;
        auto t0 = std::chrono::steady_clock::now();
        auto* ship = eng.services.get<ship::IShip>();
        gameplay::Vec3d ref{}, sv{};
        if (ship) { auto p = ship->position(); auto v = ship->velocity(); ref = {p.x, p.y, p.z}; sv = {v.x, v.y, v.z}; }
        else if (pool_.n > 0) ref = {pool_.px[0], pool_.py[0], pool_.pz[0]};              // no ship: chunks just drift (nothing to scoop them)
        gameplay::advanceChunks(pool_, dt, lifetime_, range_, ref);
        if (ship && ship->status().alive) scoop(eng, ref, sv);
        cpuMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        steps_++;
        if (eng.time() - lastReport_ >= 5.0) {
            lastReport_ = eng.time();
            LOG_D("mining", "%d chunks alive, CPU per step %.4f ms", pool_.n, steps_ ? cpuMs_ / steps_ : 0.0);
            cpuMs_ = 0; steps_ = 0;
        }
    }

private:
    uint16_t oreIndex(const std::string& id) const {
        for (size_t i = 0; i < oreIds_.size(); i++) if (oreIds_[i] == id) return (uint16_t)i;
        return 0;                                                                       // unknown ore: worthless "rock"
    }

    void onDestroyed(const world::AsteroidDestroyed& e) {
        if (!active_) return;
        int total = gameplay::totalYield(e.radius, yieldScale_);
        int n = gameplay::chunkCount(total, countScale_);
        std::vector<int>& parts = parts_;
        gameplay::splitAmount(total, n, parts);
        uint16_t oi = oreIndex(e.ore);
        for (int amount : parts) pool_.spawn(e.position, gameplay::chunkVelocity(rng_, 2.0f, 10.0f), amount, oi);
        LOG_I("mining", "asteroid %d (%s, radius %.1f) broke into %zu chunks holding %d ore units", e.id, oreIds_[oi].c_str(), e.radius, parts.size(), total);
    }

    void scoop(engine::Engine& eng, const gameplay::Vec3d& ship, const gameplay::Vec3d& sv) {
        auto* inv = eng.services.get<gameplay::IInventory>();
        if (!inv) return;
        for (int i = pool_.n - 1; i >= 0; i--) {
            double dx = pool_.px[i] - ship.x, dy = pool_.py[i] - ship.y, dz = pool_.pz[i] - ship.z, d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d > scoopRadius_) { pool_.blocked[i] = 0; continue; }
            if (pool_.blocked[i]) continue;                                             // the hold said no: try again after leaving and coming back
            double rvx = sv.x - pool_.vx[i], rvy = sv.y - pool_.vy[i], rvz = sv.z - pool_.vz[i], rel = std::sqrt(rvx * rvx + rvy * rvy + rvz * rvz);
            if (!gameplay::canScoop(d, rel, scoopRadius_, scoopSpeed_)) continue;       // too fast: it flies by
            const std::string& id = oreIds_[pool_.ore[i]];
            int got = inv->add(id, pool_.amount[i]);                                    // partial when the hold is nearly full; the inventory emits CargoFull
            if (got > 0) {
                eng.events.emit(gameplay::OreMined{id, got});
                LOG_I("mining", "scooped %d %s (cargo %.0f / %.0f)", got, id.c_str(), inv->used(), inv->capacity());
                fx::SpawnParticles s; s.kind = "spark"; s.position = {pool_.px[i], pool_.py[i], pool_.pz[i]}; s.count = 4; s.size = 0.5f;
                for (int k = 0; k < 3; k++) s.colour[k] = oreCol_[pool_.ore[i] * 3 + k];
                eng.events.emit(s);
                if (auto* audio = eng.services.get<core::IAudio>()) if (audio->hasSound("pickup")) audio->play("pickup", 0.5f);
            }
            if (got >= pool_.amount[i]) pool_.remove(i);
            else {
                pool_.amount[i] -= got;
                pool_.blocked[i] = 1;
                LOG_I("mining", "hold full: %d %s left in the chunk", pool_.amount[i], id.c_str());
            }
        }
    }

    // one glDrawArrays(GL_QUADS): camera-facing squares in the ore colour, at least ~3 pixels wide at any distance
    void draw(core::RenderEngine& r) {
        if (pool_.n == 0) return;
        auto t0 = std::chrono::steady_clock::now();
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
        for (int i = 0; i < pool_.n; i++) {
            double dx = pool_.px[i] - cam.x, dy = pool_.py[i] - cam.y, dz = pool_.pz[i] - cam.z, dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dx * fx + dy * fy + dz * fz <= 0.5) continue;                            // behind the camera
            float h = (float)std::max(0.25 + 0.06 * std::sqrt((double)pool_.amount[i]), dist * 0.0045);
            const float* c = &oreCol_[pool_.ore[i] * 3];
            float* q = &quads_[(size_t)verts * 3]; float* col = &quadCol_[(size_t)verts * 3];
            const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
            for (int k = 0; k < 4; k++) {
                q[k * 3] = (float)dx + (rx * sx[k] + ux * sy[k]) * h; q[k * 3 + 1] = (float)dy + (ry * sx[k] + uy * sy[k]) * h; q[k * 3 + 2] = (float)dz + (rz * sx[k] + uz * sy[k]) * h;
                col[k * 3] = std::min(1.0f, c[0] * 1.2f + 0.15f); col[k * 3 + 1] = std::min(1.0f, c[1] * 1.2f + 0.15f); col[k * 3 + 2] = std::min(1.0f, c[2] * 1.2f + 0.15f);
            }
            verts += 4;
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
        drawMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }

    engine::Engine* eng_ = nullptr;
    core::RenderEngine* render_ = nullptr;
    bool active_ = false;
    float yieldScale_ = 0.5f, countScale_ = 1.0f, lifetime_ = 300.0f, range_ = 3000.0f, scoopRadius_ = 12.0f, scoopSpeed_ = 40.0f;
    double cpuMs_ = 0, drawMs_ = 0, lastReport_ = -10;
    long steps_ = 0;
    gameplay::ChunkPool pool_;
    gameplay::Rng rng_;
    std::vector<std::string> oreIds_;
    std::vector<float> oreCol_, quads_, quadCol_;
    std::vector<int> parts_;
};

REGISTER_MODULE(Mining);
