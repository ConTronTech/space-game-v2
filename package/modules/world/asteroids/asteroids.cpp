// world/asteroids - belts and clusters of static asteroids: seeded, culled, LOD-drawn, with a small pool of physics bodies around the ship.
// Provides world::IAsteroids. Rules in asteroid_rules.h; behaviour, tunables and laptop notes in docs/WORLD.md.
#include <GL/gl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include "core/data_registry/data_api.h"
#include "core/physics_world/physics_api.h"
#include "core/render_engine/render_engine.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "ship/ship_core/ship_api.h"
#include "world/asteroids/asteroid_rules.h"
#include "world/asteroids/asteroids_api.h"
#include "world/star_system/star_system_api.h"

class Asteroids : public engine::Module, public world::IAsteroids {
public:
    const char* name() const override { return "world/asteroids"; }
    std::vector<std::string> dependencies() const override { return {"core/render_engine"}; }
    std::vector<std::string> optionalDependencies() const override {
        return {"world/star_system", "core/physics_world", "core/data_registry", "ship/ship_core", "ship/fake_ship"};
    }

    bool init(engine::Engine& eng) override {
        auto& c = eng.config;
        if (!c.get("asteroids.enabled", true, "generate and draw asteroid belts and clusters")) return true;
        world::GenParams gp;
        gp.seed = (unsigned)c.get("world.seed", 1234.0f, "seed of the star system (same seed = same system)");
        gp.beltCount = c.get("asteroids.belt_count", 1, "asteroid belts between planet orbits");
        gp.beltAsteroids = std::clamp(c.get("asteroids.belt_asteroids", 1500, "asteroids per belt"), 0, 50000);
        gp.clusterCount = c.get("asteroids.cluster_count", 4, "asteroid clusters around planets and moons");
        gp.clusterAsteroids = std::clamp(c.get("asteroids.cluster_asteroids", 60, "asteroids per cluster"), 0, 5000);
        drawDist_ = c.get("asteroids.draw_distance", 6000.0f, "asteroids farther than this are not drawn, units");
        maxDrawn_ = std::max(1, c.get("asteroids.max_drawn", 400, "most asteroid meshes drawn per frame (nearest first); the rest become points"));
        physR_ = c.get("asteroids.physics_radius", 1500.0f, "only asteroids this close to the ship have physics bodies, units");
        maxBodies_ = std::max(1, c.get("asteroids.max_bodies", 300, "most asteroid physics bodies alive at once"));
        triBudget_ = std::max(100, c.get("asteroids.triangle_budget", 15000, "most asteroid triangles drawn per frame"));
        edgePx_ = std::max(2.0f, c.get("asteroids.lod_edge_px", 10.0f, "target on-screen size of a mesh edge in pixels"));

        // ore table from data/ores.json (optional)
        if (auto* data = eng.services.get<core::IData>()) {
            for (auto& id : data->ids("ores")) {
                const engine::Json& o = data->get("ores", id);
                ores_.ids.push_back(id);
                ores_.weights.push_back((float)o["rarity"].num(1.0));
                for (int k = 0; k < 3; k++) ores_.color.push_back((float)o["color"].at(k).num(0.5));
            }
        }
        if (ores_.ids.empty()) { ores_.ids = {"rock"}; ores_.weights = {1.0f}; ores_.color = {0.45f, 0.42f, 0.40f}; }

        auto* sys = eng.services.get<world::IStarSystem>();
        if (!sys) { LOG_I("asteroids", "no star system: no asteroids"); return true; }
        std::vector<double> orbits;
        std::vector<world::TargetBody> targets;
        for (auto& b : sys->bodies()) {
            if (b.kind == world::BodyKind::Planet) orbits.push_back(b.orbitRadius);
            if (b.kind != world::BodyKind::Sun) targets.push_back({b.position, b.radius, b.kind == world::BodyKind::Moon});
        }
        auto t0 = std::chrono::steady_clock::now();
        std::vector<world::BeltInfo> belts;
        field_ = world::generateField(gp, sys->sunPosition(), orbits, targets, ores_, &belts);
        for (int v = 0; v < world::kAsteroidVariants; v++)
            for (int l = 0; l < world::kAsteroidLevels; l++) { meshes_[v][l] = world::buildAsteroidMesh(l, world::mixSeed(gp.seed, 400 + v)); meshBytes_ += meshes_[v][l].bytes(); }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        int n = field_.count();
        LOG_I("asteroids", "%d asteroids (%zu belts, %d clusters) and %d shared meshes generated in %.2f ms, %.0f KB", n, belts.size(), std::min<int>(gp.clusterCount, (int)targets.size()),
              world::kAsteroidVariants * world::kAsteroidLevels, ms, (double)(field_.bytes() + meshBytes_) / 1024.0);
        for (auto& b : belts) LOG_D("asteroids", "belt: %.0f .. %.0f from the sun, +-%.0f thick", b.inner, b.outer, b.halfHeight);

        cand_.reserve(n); levels_.reserve(n); pts_.reserve((size_t)n * 3); addList_.reserve(n);
        bodyIds_.assign(n, core::kNoBody);
        physics_ = eng.services.get<core::IPhysics>();
        eng_ = &eng;
        render_ = &eng.services.require<core::RenderEngine>();
        render_->addPass("asteroids", 60, [this](core::RenderEngine& r) { draw(r); });   // after the star system (50), before the demo rocks (100)
        eng.services.provide<world::IAsteroids>(this);
        active_ = true;
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        if (!active_) return;
        render_->removePass("asteroids");
        eng.services.withdraw<world::IAsteroids>();
        if (physics_) for (int& id : bodyIds_) if (id != core::kNoBody) { physics_->removeBody(id); id = core::kNoBody; }
    }

    // ---- world::IAsteroids ----
    int count() const override { return field_.count(); }
    world::Vec3d position(int i) const override { return ok(i) ? world::Vec3d{field_.x[i], field_.y[i], field_.z[i]} : world::Vec3d{}; }
    float radius(int i) const override { return ok(i) ? field_.radius[i] : 0.0f; }
    std::string ore(int i) const override { return ok(i) && field_.ore[i] < ores_.ids.size() ? ores_.ids[field_.ore[i]] : "rock"; }
    void nearest(const world::Vec3d& p, int n, std::vector<int>& out) const override { world::nearestIndices(field_, p, n, out); }

    // ---- physics pool: static bodies only for asteroids near the ship ----
    void onFixedUpdate(engine::Engine& eng, float) override {
        if (!active_ || !physics_) return;
        auto* ship = eng.services.get<ship::IShip>();
        if (!ship) return;
        auto sp = ship->position();
        const double R = physR_, hyst = 1.2;
        addList_.clear();
        int removed = 0;
        for (int i = 0; i < field_.count(); i++) {
            double dx = field_.x[i] - sp.x, dy = field_.y[i] - sp.y, dz = field_.z[i] - sp.z;
            double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            int act = world::poolAction(d - field_.radius[i], bodyIds_[i] != core::kNoBody, R, hyst);
            if (act < 0 && removed < kMaxChanges * 2) { physics_->removeBody(bodyIds_[i]); bodyIds_[i] = core::kNoBody; live_--; removed++; }
            else if (act > 0) addList_.push_back({d, i});
        }
        int room = std::min(kMaxChanges, maxBodies_ - live_);
        if (room > 0 && !addList_.empty()) {
            int n = std::min<int>(room, (int)addList_.size());
            std::partial_sort(addList_.begin(), addList_.begin() + n, addList_.end());   // nearest first
            for (int k = 0; k < n; k++) {
                int i = addList_[k].second;
                bodyIds_[i] = physics_->addBody("asteroid", {(float)field_.x[i], (float)field_.y[i], (float)field_.z[i]}, field_.radius[i] * 0.9f, false);
                live_++;
            }
        }
    }

private:
    static constexpr int kMaxChanges = 32;    // most bodies added per fixed step (and twice that removed)

    bool ok(int i) const { return i >= 0 && i < field_.count(); }

    struct Cand { double dist; int index; float px; };

    void draw(core::RenderEngine& r) {
        const int n = field_.count();
        if (n == 0) return;
        float m[16];
        for (int i = 0; i < 16; i++) m[i] = r.camera.view[i];
        world::Vec3d cam{-((double)m[0] * m[12] + (double)m[1] * m[13] + (double)m[2] * m[14]),
                         -((double)m[4] * m[12] + (double)m[5] * m[13] + (double)m[6] * m[14]),
                         -((double)m[8] * m[12] + (double)m[9] * m[13] + (double)m[10] * m[14])};
        const double fx = -m[2], fy = -m[6], fz = -m[10];               // the camera looks along -Z of view space
        m[12] = m[13] = m[14] = 0;
        glLoadMatrixf(m);

        GLint vp[4] = {0, 0, 1280, 720};
        glGetIntegerv(GL_VIEWPORT, vp);
        const double aspect = vp[3] > 0 ? (double)vp[2] / vp[3] : 1.0;
        const double half = world::viewConeHalfAngle(r.camera.fovDeg, aspect) + 0.05;
        const double cosHalf = std::cos(std::min(half, 3.0));
        const float focal = (float)vp[3] * 0.5f / std::tan(r.camera.fovDeg * 3.14159265f / 360.0f);

        cand_.clear(); pts_.clear();
        for (int i = 0; i < n; i++) {
            double dx = field_.x[i] - cam.x, dy = field_.y[i] - cam.y, dz = field_.z[i] - cam.z;
            double d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > (double)drawDist_ * drawDist_) continue;
            double d = std::sqrt(d2);
            if (!world::inViewCone(d, dx * fx + dy * fy + dz * fz, field_.radius[i], cosHalf)) continue;
            float px = d > field_.radius[i] ? (float)(field_.radius[i] / d) * focal : 1.0e6f;
            if (world::asteroidLod(px, edgePx_, kPointPx) < 0) { pts_.push_back((float)dx); pts_.push_back((float)dy); pts_.push_back((float)dz); }
            else cand_.push_back({d, i, px});
        }
        // nearest first, at most maxDrawn meshes; the rest of the would-be meshes are drawn as points
        size_t keep = std::min<size_t>(cand_.size(), (size_t)maxDrawn_);
        if (keep < cand_.size()) std::nth_element(cand_.begin(), cand_.begin() + keep, cand_.end(), [](const Cand& a, const Cand& b) { return a.dist < b.dist; });
        std::sort(cand_.begin(), cand_.begin() + keep, [](const Cand& a, const Cand& b) { return a.dist < b.dist; });
        for (size_t k = keep; k < cand_.size(); k++) {
            int i = cand_[k].index;
            pts_.push_back((float)(field_.x[i] - cam.x)); pts_.push_back((float)(field_.y[i] - cam.y)); pts_.push_back((float)(field_.z[i] - cam.z));
        }
        levels_.clear();
        for (size_t k = 0; k < keep; k++) levels_.push_back(world::asteroidLod(cand_[k].px, edgePx_, kPointPx));
        int tris = world::applyAsteroidBudget(levels_, triBudget_);
        for (size_t k = 0; k < keep; k++)
            if (levels_[k] < 0) { int i = cand_[k].index; pts_.push_back((float)(field_.x[i] - cam.x)); pts_.push_back((float)(field_.y[i] - cam.y)); pts_.push_back((float)(field_.z[i] - cam.z)); }

        glPushAttrib(GL_ENABLE_BIT | GL_LIGHTING_BIT | GL_CURRENT_BIT | GL_POINT_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT);
        setLight(cam);
        glEnable(GL_LIGHTING); glEnable(GL_LIGHT0);
        glEnable(GL_COLOR_MATERIAL); glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
        glEnable(GL_NORMALIZE);
        glDisable(GL_TEXTURE_2D);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        const float t = (float)(eng_ ? eng_->time() : 0.0);
        const world::AsteroidMesh* bound = nullptr;
        int drawn = 0;
        for (size_t k = 0; k < keep; k++) {
            if (levels_[k] < 0) continue;
            int i = cand_[k].index;
            const world::AsteroidMesh& mesh = meshes_[field_.variant[i]][levels_[k]];
            if (&mesh != bound) {
                const char* base = (const char*)mesh.verts.data();
                glVertexPointer(3, GL_FLOAT, world::kAsteroidStride, base);
                glNormalPointer(GL_FLOAT, world::kAsteroidStride, base + 3 * sizeof(float));
                bound = &mesh;
            }
            const float* oc = &ores_.color[std::min<size_t>(field_.ore[i], ores_.ids.size() - 1) * 3];
            glColor3f(std::min(1.0f, oc[0] * 1.3f + 0.1f), std::min(1.0f, oc[1] * 1.3f + 0.1f), std::min(1.0f, oc[2] * 1.3f + 0.1f));
            glPushMatrix();
            glTranslatef((float)(field_.x[i] - cam.x), (float)(field_.y[i] - cam.y), (float)(field_.z[i] - cam.z));
            glRotatef(field_.spinPhase[i] + field_.spinRate[i] * t, field_.ax[i], field_.ay[i], field_.az[i]);
            glScalef(field_.radius[i], field_.radius[i], field_.radius[i]);
            glDrawElements(GL_TRIANGLES, (GLsizei)mesh.indices.size(), GL_UNSIGNED_SHORT, mesh.indices.data());
            glPopMatrix();
            drawn++;
        }
        glDisableClientState(GL_NORMAL_ARRAY);
        if (!pts_.empty()) {   // the distant ones: one batch of points
            glDisable(GL_LIGHTING);
            glPointSize(1.6f);
            glColor3f(0.6f, 0.55f, 0.5f);
            glVertexPointer(3, GL_FLOAT, 0, pts_.data());
            glDrawArrays(GL_POINTS, 0, (GLsizei)(pts_.size() / 3));
        }
        glDisableClientState(GL_VERTEX_ARRAY);
        glPopAttrib();
        report(drawn, tris, (int)(pts_.size() / 3));
    }

    // light from the sun (the direction from the camera to it, good enough across a 6,000-unit view), else a fixed light
    void setLight(const world::Vec3d& cam) {
        float dir[4] = {0.5f, 0.8f, 0.3f, 0.0f}, diffuse[4] = {1, 1, 1, 1};
        if (eng_) if (auto* sys = eng_->services.get<world::IStarSystem>()) {
            world::Vec3d s = sys->sunPosition();
            double dx = s.x - cam.x, dy = s.y - cam.y, dz = s.z - cam.z, l = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (l > 1e-3) { dir[0] = (float)(dx / l); dir[1] = (float)(dy / l); dir[2] = (float)(dz / l); }
            if (!sys->bodies().empty()) for (int k = 0; k < 3; k++) diffuse[k] = sys->bodies()[0].color[k];
        }
        const float ambient[4] = {0, 0, 0, 1}, globalAmbient[4] = {0.08f, 0.08f, 0.1f, 1};
        glLightfv(GL_LIGHT0, GL_POSITION, dir);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
        glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, globalAmbient);
    }

    void report(int drawn, int tris, int points) {
        if (!eng_ || eng_->time() - lastReport_ < 2.0) return;
        lastReport_ = eng_->time();
        LOG_D("asteroids", "%d meshes drawn (%d triangles), %d points, %d physics bodies alive, of %d asteroids", drawn, tris, points, live_, field_.count());
    }

    static constexpr float kPointPx = 2.0f;

    engine::Engine* eng_ = nullptr;
    core::RenderEngine* render_ = nullptr;
    core::IPhysics* physics_ = nullptr;
    bool active_ = false;
    float drawDist_ = 6000, physR_ = 1500, edgePx_ = 10;
    int maxDrawn_ = 400, maxBodies_ = 300, triBudget_ = 15000, live_ = 0;
    double lastReport_ = -10;
    size_t meshBytes_ = 0;
    world::AsteroidField field_;
    world::OreTable ores_;
    world::AsteroidMesh meshes_[world::kAsteroidVariants][world::kAsteroidLevels];
    std::vector<int> bodyIds_, levels_;
    std::vector<Cand> cand_;
    std::vector<float> pts_;
    std::vector<std::pair<double, int>> addList_;
};

REGISTER_MODULE(Asteroids);
