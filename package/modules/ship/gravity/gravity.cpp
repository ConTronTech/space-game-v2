// ship/gravity - single-body (sphere-of-influence, "patched conic") gravity on the ship. See docs/GRAVITY.md.
// Every fixed step: pick the dominant body (innermost SOI containing the ship, with hysteresis) and kick the ship's velocity by a * dt through
// IShip::setVelocity; ship_core then drifts position += velocity * dt itself (semi-implicit Euler). The bodies stay on their analytic rails.
#include <GL/gl.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "core/debugger/debugger_api.h"
#include "core/render_engine/render_engine.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "ship/docking/docking_api.h"
#include "ship/gravity/gravity_rules.h"
#include "ship/orbit_lock/orbit_lock_api.h"
#include "ship/ship_core/ship_api.h"
#include "world/star_system/star_system_api.h"
#include "world/stations/stations_api.h"

class ShipGravity : public engine::Module {
public:
    const char* name() const override { return "ship/gravity"; }
    std::vector<std::string> dependencies() const override { return {"ship/ship_core"}; }
    // after the star system (this step's body positions) and the orbit lock (its lock state); the debugger only for the optional hooks
    std::vector<std::string> optionalDependencies() const override { return {"world/star_system", "ship/orbit_lock", "ship/docking", "world/stations", "ship/warp_drive", "core/debugger"}; }

    bool init(engine::Engine& eng) override {
        auto& c = eng.config;
        enabled_ = c.get("ship.gravity_enabled", true, "single-body (sphere of influence) gravity on the ship; false = pure Newtonian float, exactly the pre-gravity flight feel");
        if (eng.hasFlag("auto-gravity")) enabled_ = true;
        if (eng.hasFlag("no-gravity")) enabled_ = false;
        scale_ = c.get("orbit.gravity_scale", 60.0f, "orbit gravity: mu = scale * radius^2, circular speed = sqrt(mu / distance from the body centre)");
        minRadius_ = c.get("gravity.min_radius", 0.0f, "gravity stops growing inside this distance from a body's centre, units (0 = the body's own radius)");
        maxAccel_ = c.get("gravity.max_accel", 200.0f, "cap on the gravity acceleration, m/s^2 (the surface gravity of every body is orbit.gravity_scale, 60 by default: this never limits a real orbit)");
        hysteresis_ = std::max(1.0f, c.get("gravity.soi_hysteresis", 1.05f, "the dominant body is kept until the ship is this many times its sphere of influence away"));
        sunRange_ = std::max(0.0f, c.get("gravity.sun_range", 2.0f, "the sun pulls out to this many times the outermost planet's orbit radius; beyond it, no gravity at all"));
        assistRange_ = c.get("gravity.dock_assist_range", 0.0f, "gravity fades out from this distance to the nearest station's centre, units (0 = 4 x its dock radius, the HUD's DOCK prompt range)");
        assistMin_ = std::clamp(c.get("gravity.dock_assist_min", 0.1f, "fraction of gravity left inside the station's dock zone (1 = no assist, the old behaviour)"), 0.0f, 1.0f);
        gradientOn_ = c.get("gravity.debug_gradient_rings", true, "F7 debug draw: rings at 1.1/1.5/2/3/5/8 body radii around the dominant body, coloured by local |g| (blue weak -> red strong); the numbers are the gravity.gradient watch");
        assistOn_ = !eng.hasFlag("gravity-dock-assist-off");
        testFrame_ = frameFlag(eng, "gravity-test");
        scenario_ = eng.flagValue("gravity-scenario");
        scenarioBody_ = std::max(1, std::atoi(eng.flagValue("gravity-body", "1").c_str()));
        eng.events.subscribe<ship::OrbitLockChanged>([this](const ship::OrbitLockChanged& e) { locked_ = e.locked; });
        eng.events.subscribe<ship::Respawned>([this](const ship::Respawned&) { dominant_ = -1; });
        eng_ = &eng;
        if (!eng.services.get<world::IStarSystem>()) LOG_I("gravity", "no star system: gravity does nothing");
        LOG_I("gravity", "gravity %s (scale %.1f, hysteresis %.2f, sun range %.1fx the outermost orbit)", enabled_ ? "ON" : "off", scale_, hysteresis_, sunRange_);
        if ((dbg_ = eng.services.get<core::IDebug>())) {
            dbg_->watch("gravity.dominant", [this] { return dominant_ >= 0 ? dominantName_ : std::string(enabled_ ? "none (deep space)" : "off"); });
            dbg_->watch("gravity.accel", [this] { char b[96]; std::snprintf(b, sizeof b, "(%.2f, %.2f, %.2f)", accel_.x, accel_.y, accel_.z); return std::string(b); });
            dbg_->watch("gravity.local_g", [this] { char b[64]; std::snprintf(b, sizeof b, "%.3f m/s^2", orbit::length(accel_)); return std::string(b); });
            dbg_->watch("gravity.dock_assist", [this] { char b[64]; std::snprintf(b, sizeof b, "x%.3f (station %.0f)", assist_, stationDist_); return std::string(b); });
            dbg_->watch("gravity.soi", [this] { char b[96]; std::snprintf(b, sizeof b, "dist %.0f / soi %.0f", dist_, soi_); return std::string(b); });
            dbg_->watch("gravity.gradient", [this] { return gradientText(); });
            dbg_->drawHook("gravity.draw", [this](core::RenderEngine& r) { draw(r); });
        }
        return true;
    }

    void shutdown(engine::Engine&) override {
        if (dbg_) {
            for (const char* w : {"gravity.dominant", "gravity.accel", "gravity.local_g", "gravity.dock_assist", "gravity.soi", "gravity.gradient"}) dbg_->unwatch(w);
            dbg_->removeDrawHook("gravity.draw");
            dbg_ = nullptr;
        }
    }

    void onFixedUpdate(engine::Engine& eng, float dt) override {
        auto* sys = eng.services.get<world::IStarSystem>();
        auto* ship = eng.services.get<ship::IShip>();
        accel_ = {};
        if (!sys || !ship) return;
        const auto& bodies = sys->bodies();
        gravity::buildSources(bodies, scale_, sunRange_, src_);
        trackBodyVelocity(bodies, dt);
        long f = (long)eng.frame();
        if (!scenario_.empty() && f == kScenarioFrame) startScenario(ship);
        if (scenario_ == "dock" && f >= kScenarioFrame) steerDockScenario(eng, ship);

        const auto& st = ship->status();
        bool held = false, found = false;
        double stDist = 0, zone = 0;
        if (auto* dock = eng.services.get<ship::IDocking>()) {
            held = dock->busy();
            std::string nm, why; float d = 0; bool ok = false;
            if (!held && (found = dock->nearestDockable(nm, d, ok, why))) {
                stDist = d;
                world::Vec3d p0 = ship->positionD();
                if (auto* sts = eng.services.get<world::IStations>()) { int i = sts->nearest(p0); if (i >= 0) zone = sts->info(i).radius; }
            }
        }
        double range = assistRange_ > 0 ? assistRange_ : 4.0 * zone;
        assist_ = gravity::dockAssist(found && zone > 0, assistOn_, stDist, zone, range, assistMin_);
        stationDist_ = found ? stDist : -1;
        world::Vec3d pos = ship->positionD();       // the ship's true (double) position: everything below is body-relative math
        int prev = dominant_;
        dominant_ = gravity::dominantBody(src_, pos, dominant_, hysteresis_);
        if (dominant_ != prev) {
            dominantName_ = dominant_ >= 0 ? bodies[(size_t)dominant_].name : std::string();
            LOG_D("gravity", "dominant body: %s", dominant_ >= 0 ? dominantName_.c_str() : "none (deep space)");
            if (dbg_) dbg_->logEvent("gravity: dominant body " + (dominant_ >= 0 ? dominantName_ : std::string("none (deep space)")));
        }
        dist_ = soi_ = 0;
        if (dominant_ >= 0) { dist_ = orbit::length(orbit::sub(pos, src_[(size_t)dominant_].pos)); soi_ = src_[(size_t)dominant_].soi; }
        if (!gravity::shouldApply(enabled_, st.alive, held, st.warping, locked_, eng.paused()) || dominant_ < 0) {
            scenarioLog(eng, ship, dt);
            testLog(f, ship);
            return;
        }
        const auto& s = src_[(size_t)dominant_];
        accel_ = orbit::mul(gravity::acceleration(s, pos, minRadius_, maxAccel_), assist_);
        auto v = ship->velocity();
        world::Vec3d nv = gravity::kick({v.x, v.y, v.z}, accel_, dt);
        ship->setVelocity({(float)nv.x, (float)nv.y, (float)nv.z});

        if ((logTimer_ += dt) >= 1.0f) {
            logTimer_ = 0;
            if (engine::log::level() <= engine::log::Level::Debug) {   // the numbers are only computed when they will be printed
                world::Vec3d bv = (size_t)dominant_ < bodyVel_.size() ? bodyVel_[(size_t)dominant_] : world::Vec3d{};
                auto oi = gravity::orbitInfo(orbit::sub(pos, s.pos), orbit::sub(nv, bv), s.mu);
                LOG_D("gravity", "%s: distance %.0f (altitude %.0f), soi %.0f, g %.3f m/s^2, energy %.1f, eccentricity %.3f", dominantName_.c_str(), dist_, dist_ - s.radius, s.soi, orbit::length(accel_), oi.energy, oi.ecc);
            }
        }
        scenarioLog(eng, ship, dt);
        testLog(f, ship);
    }

private:
    static long frameFlag(engine::Engine& eng, const char* key) {
        std::string v = eng.flagValue(key);
        return v.empty() ? -1 : std::atol(v.c_str());
    }

    void trackBodyVelocity(const std::vector<world::Body>& bodies, float dt) {
        if (prevPos_.size() != bodies.size()) { prevPos_.assign(bodies.size(), {}); bodyVel_.assign(bodies.size(), {}); prevValid_ = false; }
        for (size_t i = 0; i < bodies.size(); i++) {
            world::Vec3d v = orbit::mul(orbit::sub(bodies[i].position, prevPos_[i]), 1.0 / dt);
            if (prevValid_ && orbit::length(v) < 20000.0) bodyVel_[i] = v;
            prevPos_[i] = bodies[i].position;
        }
        prevValid_ = true;
    }

    // --gravity-test=FRAME: one info line with the dominant body, SOI and acceleration at that frame
    void testLog(long f, ship::IShip* ship) {
        if (testFrame_ < 0 || f != testFrame_) return;
        auto v = ship->velocity();
        LOG_I("gravity", "test frame %ld: dominant %s, distance %.1f, soi %.1f, accel (%.3f, %.3f, %.3f) |a| %.3f m/s^2, ship speed %.2f, assist %.3f (station %.1f), gravity %s%s",
              f, dominant_ >= 0 ? dominantName_.c_str() : "none", dist_, soi_, accel_.x, accel_.y, accel_.z, orbit::length(accel_),
              orbit::length({v.x, v.y, v.z}), assist_, stationDist_, enabled_ ? "on" : "off", locked_ ? " (orbit-locked: skipped)" : "");
    }

    // --gravity-scenario=orbit|drop|deep (dev): places the ship at frame kScenarioFrame, then logs altitude/speed once per simulated second.
    //   orbit: 1.5 radii above body --gravity-body (default 1 = Planet 1), at the orbit guide's own circular speed (plus the body's velocity)
    //   drop:  the same spot, at rest relative to the body;   deep: 2.5x the sun range away from the sun, drifting at 10 m/s
    void startScenario(ship::IShip* ship) {
        int b = std::min(scenarioBody_, (int)src_.size() - 1);
        const auto& s = src_[(size_t)b];
        world::Vec3d bv = (size_t)b < bodyVel_.size() ? bodyVel_[(size_t)b] : world::Vec3d{};
        world::Vec3d pos, vel;
        if (scenario_ == "dock" || scenario_ == "dockmiss") {   // dev: a scripted, slow approach straight down onto a station's pad (see steerDockScenario)
            auto* sts = eng_->services.get<world::IStations>();
            dockStation_ = -1;
            for (int i = 0; sts && i < sts->count(); i++) if (sts->info(i).kind == world::StationKind::Planetary) { dockStation_ = i; break; }
            if (dockStation_ < 0) { LOG_W("gravity", "scenario %s: no planetary station", scenario_.c_str()); scenario_.clear(); return; }
            auto si = sts->info(dockStation_);
            // dockmiss: the same altitude on the far side of the station's planet (no station anywhere near): gravity must be untouched
            world::Vec3d dir = scenario_ == "dock" ? si.up : orbit::mul(si.up, -1.0);
            const auto& par = src_[(size_t)si.parent];
            double r0 = orbit::length(orbit::sub(si.position, par.pos)) + 3000.0;
            pos = scenario_ == "dock" ? orbit::add(si.position, orbit::mul(si.up, 1000.0)) : orbit::add(par.pos, orbit::mul(dir, r0));
            vel = si.velocity;
            scenarioRef_ = si.parent;
            ship->setPoseD(pos, {0, 0, 1}, {(float)si.up.x, (float)si.up.y, (float)si.up.z});
            ship->setVelocity({(float)vel.x, (float)vel.y, (float)vel.z});
            scenarioT_ = 0; scenarioLogT_ = 1.0;
            LOG_I("gravity", "scenario %s: station %s (zone %.0f), start %.1f from it", scenario_.c_str(), si.name.c_str(), si.radius, orbit::length(orbit::sub(pos, si.position)));
            return;
        } else if (scenario_ == "deep") {
            pos = orbit::add(src_[0].pos, {src_[0].soi * 2.5, 0, 0});
            vel = {0, 0, 10};
            scenarioRef_ = -1;
        } else {
            double r = s.radius * 2.5;
            pos = orbit::add(s.pos, {r, 0, 0});
            vel = bv;
            if (scenario_ == "orbit") vel = orbit::add(vel, {0, 0, orbit::circularSpeed(s.mu, r)});
            scenarioRef_ = b;
        }
        ship->setPoseD(pos, {0, 0, 1}, {0, 1, 0});
        ship->setVelocity({(float)vel.x, (float)vel.y, (float)vel.z});
        scenarioT_ = 0; scenarioLogT_ = 1.0;
        LOG_I("gravity", "scenario %s: body %d, start distance %.1f, speed %.2f (relative %.2f)", scenario_.c_str(), b, orbit::length(orbit::sub(pos, s.pos)), orbit::length(vel), orbit::length(orbit::sub(vel, bv)));
    }

    // dock scenario: a "pilot" holds a steady 10 m/s descent (relative to the station) toward the pad, stopping 40 units above its centre.
    // It sets the velocity BEFORE gravity's kick, so each step's kick is exactly what the pilot has to fight (logged as |a| below).
    void steerDockScenario(engine::Engine& eng, ship::IShip* ship) {
        auto* sts = eng.services.get<world::IStations>();
        auto* dock = eng.services.get<ship::IDocking>();
        if (!sts || dockStation_ < 0 || (dock && dock->busy())) return;
        auto si = sts->info(dockStation_);
        world::Vec3d p = ship->positionD();
        double h = orbit::dot(orbit::sub(p, si.position), si.up);
        world::Vec3d v = orbit::add(si.velocity, orbit::mul(si.up, h > 40.0 ? -10.0 : 0.0));
        ship->setVelocity({(float)v.x, (float)v.y, (float)v.z});
    }

    void scenarioLog(engine::Engine& eng, ship::IShip* ship, float dt) {
        if (scenario_.empty() || (long)eng.frame() < kScenarioFrame || eng.paused()) return;
        scenarioT_ += dt;
        if ((scenarioLogT_ += dt) < 1.0) return;
        scenarioLogT_ = 0;
        world::Vec3d p = ship->positionD(); auto v = ship->velocity();
        if (scenarioRef_ < 0) { LOG_I("gravity", "scenario t=%.0f s: speed %.4f m/s, dominant %s", scenarioT_, orbit::length({v.x, v.y, v.z}), dominant_ >= 0 ? dominantName_.c_str() : "none"); return; }
        const auto& s = src_[(size_t)scenarioRef_];
        world::Vec3d bv = bodyVel_[(size_t)scenarioRef_];
        double d = orbit::length(orbit::sub(p, s.pos));
        LOG_I("gravity", "scenario t=%.0f s: altitude %.1f, relative speed %.2f m/s, alive %d, dominant %s, station %.1f, assist %.3f, |a| %.3f m/s^2", scenarioT_, d - s.radius,
              orbit::length(orbit::sub({v.x, v.y, v.z}, bv)), ship->status().alive ? 1 : 0, dominant_ >= 0 ? dominantName_.c_str() : "none", stationDist_, assist_, orbit::length(accel_));
    }

    // ---- debugger draw hook: SOI circle, acceleration vector, forecast arc (fixed buffers, no allocation) ----
    void draw(core::RenderEngine& r) {
        auto* ship = eng_ ? eng_->services.get<ship::IShip>() : nullptr;
        if (!ship || dominant_ < 0 || (size_t)dominant_ >= src_.size()) return;
        float m[16];
        for (int i = 0; i < 16; i++) m[i] = r.camera.view[i];
        world::Vec3d cam{-((double)m[0] * m[12] + (double)m[1] * m[13] + (double)m[2] * m[14]),
                         -((double)m[4] * m[12] + (double)m[5] * m[13] + (double)m[6] * m[14]),
                         -((double)m[8] * m[12] + (double)m[9] * m[13] + (double)m[10] * m[14])};
        m[12] = m[13] = m[14] = 0;
        const auto& s = src_[(size_t)dominant_];
        world::Vec3d pos = ship->positionD(); auto v = ship->velocity();
        world::Vec3d bv = (size_t)dominant_ < bodyVel_.size() ? bodyVel_[(size_t)dominant_] : world::Vec3d{};
        world::Vec3d rel = orbit::sub(pos, s.pos), relVel = orbit::sub({v.x, v.y, v.z}, bv);
        // the SOI circle in the ship's orbital plane (or the XZ plane when there is no plane)
        world::Vec3d n = orbit::normalized(orbit::cross(rel, relVel));
        if (orbit::length(n) < 0.5) n = {0, 1, 0};
        world::Vec3d u = orbit::normalized(orbit::cross(n, {1, 0, 0}));
        if (orbit::length(u) < 0.5) u = orbit::normalized(orbit::cross(n, {0, 0, 1}));
        world::Vec3d w = orbit::cross(n, u);
        auto put = [&](float* dst, const world::Vec3d& a) { world::Vec3d d = orbit::sub(a, cam); dst[0] = (float)d.x; dst[1] = (float)d.y; dst[2] = (float)d.z; };
        for (int i = 0; i <= kCircle; i++) {
            double t = i * 2.0 * 3.14159265358979 / kCircle;
            put(&circle_[i * 3], orbit::add(s.pos, orbit::add(orbit::mul(u, s.soi * std::cos(t)), orbit::mul(w, s.soi * std::sin(t)))));
        }
        // forecast in the body's frame, coarse dt (0.5 s per segment), drawn around the body's current position
        world::Vec3d fp[kForecast + 1];
        int nf = gravity::forecast(s, pos, relVel, 0.5, kForecast, minRadius_, maxAccel_, fp);
        for (int i = 0; i < nf; i++) put(&arc_[i * 3], fp[i]);
        double g = orbit::length(accel_);
        put(&vec_[0], pos);
        put(&vec_[3], orbit::add(pos, g > 1e-9 ? orbit::mul(accel_, std::clamp(g * 10.0, 20.0, 200.0) / g) : world::Vec3d{}));

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadMatrixf(m);
        glPushAttrib(GL_CURRENT_BIT);
        glEnableClientState(GL_VERTEX_ARRAY);
        glColor4f(0.4f, 0.7f, 1.0f, 0.5f); glVertexPointer(3, GL_FLOAT, 0, circle_); glDrawArrays(GL_LINE_STRIP, 0, kCircle + 1);
        glColor4f(1.0f, 0.9f, 0.3f, 0.8f); glVertexPointer(3, GL_FLOAT, 0, arc_); glDrawArrays(GL_LINE_STRIP, 0, nf);
        glColor4f(1.0f, 0.3f, 0.3f, 1.0f); glVertexPointer(3, GL_FLOAT, 0, vec_); glDrawArrays(GL_LINES, 0, 2);
        if (gradientOn_) {   // the gradient rings face the camera (always read as circles, never edge-on like the orbital-plane SOI ring)
            world::Vec3d fn = orbit::normalized(orbit::sub(s.pos, cam));
            world::Vec3d fu = orbit::normalized(orbit::cross(fn, {0, 1, 0}));
            if (orbit::length(fu) < 0.5) fu = orbit::normalized(orbit::cross(fn, {1, 0, 0}));
            world::Vec3d fw = orbit::cross(fn, fu);
            double gd[gravity::kGradientRings], gm[gravity::kGradientRings];
            int nr = gravity::gradientDistances(s, gd);
            for (int k = 0; k < nr; k++) gm[k] = gravity::gradientMagnitude(s, gd[k], minRadius_, maxAccel_);
            for (int k = 0; k < nr; k++) {
                for (int i = 0; i <= kCircle; i++) {
                    double t = i * 2.0 * 3.14159265358979 / kCircle;
                    put(&ring_[i * 3], orbit::add(s.pos, orbit::add(orbit::mul(fu, gd[k] * std::cos(t)), orbit::mul(fw, gd[k] * std::sin(t)))));
                }
                float c[3];
                gravity::gradientColor(gravity::gradientT(gm[k], gm[nr - 1], gm[0]), c);
                glColor4f(c[0], c[1], c[2], 0.7f); glVertexPointer(3, GL_FLOAT, 0, ring_); glDrawArrays(GL_LINE_STRIP, 0, kCircle + 1);
            }
        }
        glDisableClientState(GL_VERTEX_ARRAY);
        glPopAttrib();
        glPopMatrix();
    }

    // gravity.gradient watch: "49.6 26.7 15.0 6.7 2.4 0.9 m/s^2 @ 1.1/1.5/2/3/5/8 R (Planet 1, R 408)", numbers first so the panel's
    // truncation keeps them (built only while F6's panel or F8's dump reads it)
    std::string gradientText() const {
        if (!gradientOn_) return "off";
        if (dominant_ < 0 || (size_t)dominant_ >= src_.size()) return "none (deep space)";
        const auto& s = src_[(size_t)dominant_];
        double gd[gravity::kGradientRings], gx[gravity::kGradientRings];
        int nr = gravity::gradientDistances(s, gd, gx);
        char b[256];
        int len = 0;
        for (int k = 0; k < nr; k++) len += std::snprintf(b + len, sizeof b - len, "%.1f ", gravity::gradientMagnitude(s, gd[k], minRadius_, maxAccel_));
        len += std::snprintf(b + len, sizeof b - len, "m/s^2 @");
        for (int k = 0; k < nr; k++) len += std::snprintf(b + len, sizeof b - len, "%s%.3g", k ? "/" : " ", gx[k]);
        std::snprintf(b + len, sizeof b - len, " R (%.40s, R %.0f)", dominantName_.c_str(), s.radius);
        return std::string(b);
    }

    static constexpr long kScenarioFrame = 5;
    static constexpr int kCircle = 64, kForecast = 60;
    engine::Engine* eng_ = nullptr;
    core::IDebug* dbg_ = nullptr;
    float assistRange_ = 0, assistMin_ = 0.1f;
    double assist_ = 1, stationDist_ = -1;
    bool assistOn_ = true, gradientOn_ = true;
    bool enabled_ = true, locked_ = false, prevValid_ = false;
    float scale_ = 60, minRadius_ = 0, maxAccel_ = 200, hysteresis_ = 1.05f, sunRange_ = 2, logTimer_ = 0;
    long testFrame_ = -1;
    int dockStation_ = -1;
    int dominant_ = -1, scenarioBody_ = 1, scenarioRef_ = -1;
    double dist_ = 0, soi_ = 0, scenarioT_ = 0, scenarioLogT_ = 0;
    std::string dominantName_, scenario_;
    world::Vec3d accel_{};
    std::vector<gravity::Source> src_;
    std::vector<world::Vec3d> prevPos_, bodyVel_;
    float circle_[(kCircle + 1) * 3] = {}, arc_[(kForecast + 1) * 3] = {}, vec_[6] = {}, ring_[(kCircle + 1) * 3] = {};
};

REGISTER_MODULE(ShipGravity);
