// ship/orbit_lock - toggle_orbit_lock (O): a stable circular orbit around the body whose surface is nearest.
// The ship is steered along an analytic orbit that follows the moving body, using only IShip::setVelocity. See docs/ORBIT_LOCK.md.
#include <GL/gl.h>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include "core/input_handler/input_api.h"
#include "core/render_engine/render_engine.h"
#include "core/ui_handler/ui_handler.h"
#include "ship/docking/docking_api.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "ship/orbit_lock/orbit_lock_api.h"
#include "ship/orbit_lock/orbit_lock_rules.h"
#include "ship/ship_core/ship_api.h"
#include "world/star_system/star_system_api.h"

class OrbitLock : public engine::Module, public ship::IOrbitGuide {
public:
    const char* name() const override { return "ship/orbit_lock"; }
    std::vector<std::string> dependencies() const override { return {"core/input_handler"}; }
    // run after the ship (and the star system, so the body positions are already this step's)
    std::vector<std::string> optionalDependencies() const override { return {"ship/ship_core", "ship/fake_ship", "world/star_system", "core/render_engine", "core/ui_handler", "ship/docking"}; }

    bool init(engine::Engine& eng) override {
        input_ = &eng.services.require<core::IInput>();
        auto& c = eng.config;
        gravity_ = c.get("orbit.gravity_scale", 60.0f, "orbit gravity: mu = scale * radius^2, circular speed = sqrt(mu / distance from the body centre)");
        range_ = c.get("orbit.engage_range", 3.0f, "lock only within this many body radii above the surface");
        minAlt_ = c.get("orbit.min_altitude", 20.0f, "never lock closer than this above the surface, units");
        releaseOnThrust_ = c.get("orbit.release_on_thrust", true, "thrust, strafe, lift or brake releases the lock (you burned)");
        guideRange_ = c.get("orbit.guide_range", 6.0f, "the orbit guide (ring + HUD block) shows within this many body radii above the surface");
        guideMinRange_ = c.get("orbit.guide_min_range", 2500.0f, "... and always within this many units of the surface (small bodies): range = max(guide_range x radius, this)");
        showGuide_ = c.get("orbit.show_guide", true, "draw the orbit guide near a body: the circular orbit the lock would give, amber = misaligned, green = aligned");
        requireAlign_ = c.get("orbit.require_alignment", true, "O only locks when the ship is lined up with the orbit (false = the old snap: lock from any state)");
        tol_.speedFrac = std::max(0.0f, c.get("orbit.align_speed_tol", 25.0f, "speed tolerance for locking, percent of the circular speed")) / 100.0;
        tol_.angleDeg = std::max(0.0f, c.get("orbit.align_angle_tol", 25.0f, "heading tolerance for locking: max degrees between your velocity and the orbit tangent"));
        settleSeconds_ = std::max(0.0f, c.get("orbit.settle_seconds", 1.5f, "after a lock the ship is eased onto the exact circular orbit over this many seconds at a perfect entry, up to 3 s at the edge of the tolerances (0 = snap)"));
        segments_ = std::clamp(c.get("orbit.guide_segments", 96, "segments of the guide ring (the Low preset uses 48 and draws no tick marks; 96+ draws ticks every 30 degrees)"), 12, 256);
        autoOn_ = frameFlag(eng, "auto-orbit-lock");
        autoOff_ = frameFlag(eng, "auto-orbit-lock-off");
        eng.events.subscribe<ship::DamageTaken>([this, &eng](const ship::DamageTaken&) { if (locked_) release(eng, "hit"); });
        eng.events.subscribe<ship::Died>([this, &eng](const ship::Died&) { if (locked_) release(eng, "ship destroyed"); });
        eng.events.subscribe<ship::Respawned>([this, &eng](const ship::Respawned&) { if (locked_) release(eng, "respawn"); });

        eng_ = &eng;
        eng.services.provide<ship::IOrbitGuide>(this);
        if (showGuide_) {
            ring_.reserve((size_t)segments_ + 1);
            verts_.resize(((size_t)segments_ + 1) * 3);
            cols_.resize(((size_t)segments_ + 1) * 4);
            if ((render_ = eng.services.get<core::RenderEngine>())) render_->addPass("ship/orbit_guide", 78, [this](core::RenderEngine& r) { drawGuide(r); });
            if ((ui_ = eng.services.get<core::UIHandler>())) ui_->addPanel("ship/orbit_guide", 12, [this](core::UIHandler& ui) { drawBlock(ui); });
        }
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        locked_ = false;   // nothing to undo: the ship simply keeps its velocity
        if (render_) render_->removePass("ship/orbit_guide");
        if (ui_) ui_->removePanel("ship/orbit_guide");
        if (eng.services.get<ship::IOrbitGuide>() == static_cast<ship::IOrbitGuide*>(this)) eng.services.withdraw<ship::IOrbitGuide>();
    }

    // ---- ship::IOrbitGuide ----
    bool active() const override { return guideActive_; }
    std::string bodyName() const override { return guideActive_ ? guideName_ : std::string(); }
    float altitude() const override { return guideActive_ ? (float)align_.altitude : 0.0f; }
    float neededSpeed() const override { return guideActive_ ? (float)align_.needSpeed : 0.0f; }
    float speedError() const override { return guideActive_ ? (float)align_.speedError : 0.0f; }
    float headingErrorDeg() const override { return guideActive_ ? (float)align_.headingDeg : 0.0f; }
    bool aligned() const override { return guideActive_ && align_.aligned(); }
    std::string refuseReason() const override { return guideActive_ ? orbit::refusalText(align_) : std::string(); }

    void onUpdate(engine::Engine& eng, float dt) override {
        updateGuide(eng, dt);
        bool toggle = input_->pressed("toggle_orbit_lock");
        long f = (long)eng.frame();
        if (autoOn_ >= 0 && f == autoOn_) toggle = true;
        if (autoOff_ >= 0 && f == autoOff_) toggle = true;
        if (!toggle) return;
        if (eng.paused()) { LOG_D("orbit", "toggle ignored: game is paused"); return; }
        if (locked_) release(eng, "toggled off"); else engage(eng);
    }

    void onFixedUpdate(engine::Engine& eng, float dt) override {
        auto* sys = eng.services.get<world::IStarSystem>();
        // every body's velocity from its last two positions (read by the guide, the lock and the hold). 3.5d fix: this used to overwrite the previous
        // position in the same step it was read, so the body velocity was always 0 while unlocked and the "relative" velocity was the absolute one.
        if (sys) {
            const auto& bodies = sys->bodies();
            if (prevPos_.size() != bodies.size()) { prevPos_.assign(bodies.size(), {}); bodyVel_.assign(bodies.size(), {}); prevValid_ = false; }
            for (size_t i = 0; i < bodies.size(); i++) {
                world::Vec3d v = orbit::mul(orbit::sub(bodies[i].position, prevPos_[i]), 1.0 / dt);
                if (prevValid_ && orbit::length(v) < kMaxBodySpeed) bodyVel_[i] = v;   // a jump (a loaded game) keeps the last velocity for a step
                prevPos_[i] = bodies[i].position;
            }
            prevValid_ = true;
        }
        if (!locked_) return;
        auto* ship = eng.services.get<ship::IShip>();
        if (!ship || !sys || body_ >= (int)sys->bodies().size() || body_ >= (int)bodyVel_.size()) { release(eng, "ship or star system gone"); return; }
        const auto& st = ship->status();
        if (orbit::shouldRelease(st.alive, st.warping, input_->value("thrust"), input_->value("strafe"), input_->value("lift"),
                                 input_->down("brake"), releaseOnThrust_)) {
            release(eng, st.alive ? (st.warping ? "warp" : "thrust") : "ship destroyed");
            return;
        }
        // Where the ship must be at the next step: the body's next position (its current velocity extrapolated one step) plus the orbit offset.
        // The ship integrates position += velocity * dt, so the velocity that lands it there is (target - position) / dt. Re-derived from the
        // analytic orbit every step: errors never accumulate.
        world::Vec3d bp = sys->positionAt(body_);
        world::Vec3d bv = bodyVel_[body_];
        elapsed_ += dt;
        world::Vec3d offset;
        if (elapsed_ - dt < settleSeconds_used_) {                     // still settling: ease onto the circle, then re-anchor the analytic orbit where we are
            offset = off_ = orbit::settleStep(orbit_, relVel0_, off_, elapsed_ - dt, dt, settleSeconds_used_, floorRadius_);
            if (elapsed_ >= settleSeconds_used_) {
                orbit_ = orbit::rebaseOrbit(orbit_, off_, elapsed_);
                LOG_I("orbit", "settled on %s: radius %.1f, circular speed %.1f m/s", name_.c_str(), orbit_.radius, orbit_.speed);
            }
        } else offset = orbit::offsetAt(orbit_, elapsed_);
        world::Vec3d target = orbit::add(orbit::add(bp, orbit::mul(bv, dt)), offset);
        auto p = ship->position();
        world::Vec3d v = orbit::mul(orbit::sub(target, {p.x, p.y, p.z}), 1.0 / dt);
        ship->setVelocity({(float)v.x, (float)v.y, (float)v.z});
        if ((logTimer_ += dt) >= 1.0f) {
            logTimer_ = 0;
            world::Vec3d rel = orbit::sub({p.x, p.y, p.z}, bp);
            world::Vec3d sv = {v.x - bv.x, v.y - bv.y, v.z - bv.z};
            LOG_D("orbit", "%s: radius %.3f (start %.3f), speed %.2f m/s relative to the body", name_.c_str(), orbit::length(rel), orbit_.radius, orbit::length(sv));
        }
        if (elapsed_ < settleSeconds_used_ + 0.3 && (settleLog_ += dt) >= 0.25f) {   // the settle, at info level: the speed error shrinks to ~0
            settleLog_ = 0;
            world::Vec3d rel = orbit::sub({p.x, p.y, p.z}, bp);
            world::Vec3d sv = {v.x - bv.x, v.y - bv.y, v.z - bv.z};
            double r = orbit::length(rel), radial = r > 0 ? orbit::dot(sv, rel) / r : 0.0;
            LOG_I("orbit", "settle %.2f s: radius %.1f (lock %.1f), radial speed %+.2f m/s, speed error vs the circular orbit %.2f m/s", elapsed_, r, lockRadius_, radial, orbit::speedErrorVsCircular(orbit_, rel, sv));
        }
    }

private:
    static long frameFlag(engine::Engine& eng, const char* key) {
        std::string v = eng.flagValue(key);
        return v.empty() ? -1 : std::atol(v.c_str());
    }

    // What the guide and the lock both need: the nearest body by surface, the orbit from the ship's current state, and how aligned it is.
    struct Probe { bool ok = false; int index = -1; world::Vec3d pos, rel, relVel; double altitude = 0; float radius = 0; std::string name; orbit::Orbit orbit; orbit::Alignment align, wide; };
    bool probe(engine::Engine& eng, Probe& out, bool& fallback) {
        auto* ship = eng.services.get<ship::IShip>();
        auto* sys = eng.services.get<world::IStarSystem>();
        if (!ship || !sys) return false;
        const auto& bodies = sys->bodies();
        auto sp = ship->position();
        out.pos = {sp.x, sp.y, sp.z};
        cand_.clear();
        for (auto& b : bodies) cand_.push_back({b.position, (double)b.radius});
        auto n = orbit::nearestBody(cand_, out.pos);
        if (n.index < 0) return false;
        const auto& b = bodies[n.index];
        out.index = n.index; out.altitude = n.altitude; out.radius = b.radius; out.name = b.name;
        // body velocity from the last two steps; the ship's velocity relative to it decides which way round we orbit
        world::Vec3d bv{};
        if ((size_t)n.index < bodyVel_.size()) bv = bodyVel_[n.index];
        if (n.index != fallbackBody_) { fallbackBody_ = n.index; fallback = false; }   // the plane hysteresis is per body
        auto v = ship->velocity(), f = ship->forward();
        out.rel = orbit::sub(out.pos, b.position);
        out.relVel = orbit::sub({v.x, v.y, v.z}, bv);
        out.orbit = orbit::makeOrbit(out.rel, out.relVel, {f.x, f.y, f.z}, orbit::bodyMu(b.radius, gravity_), fallback);
        out.align = orbit::checkAlignment(out.orbit, out.rel, out.relVel, n.altitude, b.radius, range_, minAlt_, tol_);
        out.wide = orbit::checkAlignment(out.orbit, out.rel, out.relVel, n.altitude, b.radius, range_, minAlt_, orbit::widened(tol_));
        out.ok = true;
        return true;
    }

    // Every frame: is the guide showing, and how aligned is the ship? (cheap: a handful of bodies, no allocation once the vectors are sized)
    // The guide shows whenever orbit::guideVisible says so: no key press involved. The aligned state is latched (orbit::updateAlignLatch).
    void updateGuide(engine::Engine& eng, float dt) {
        bool was = guideActive_;
        guideActive_ = false;
        auto* ship = eng.services.get<ship::IShip>();
        bool docking = false;
        if (auto* dock = eng.services.get<ship::IDocking>()) docking = dock->busy();
        bool ok = ship && !locked_ && probe(eng, probe_, planeFallback_);
        const auto* st = ship ? &ship->status() : nullptr;
        if (ok) ok = orbit::guideVisible(showGuide_, locked_, st->alive, st->warping, docking, probe_.altitude, probe_.radius, guideRange_, guideMinRange_);
        if (!ok) {
            latch_ = {};
            drawNormal_ = {};                                                  // next time the guide shows, its plane starts from the raw one
            if (was) LOG_D("orbit", "guide off");
            return;
        }
        bool aligned = orbit::updateAlignLatch(latch_, probe_.align, probe_.wide, eng.paused() ? 0.0 : (double)dt);
        align_ = probe_.align;
        if (aligned) align_.reason = orbit::Refusal::None;
        if (guideName_ != probe_.name) guideName_ = probe_.name;
        guideActive_ = true;
        // the drawn plane: eased toward the raw one (small wobble damped, a real turn or a new body taken at once). Display only: the lock
        // and the alignment check use the raw orbit.
        if (probe_.index != drawBody_) { drawBody_ = probe_.index; drawNormal_ = {}; }
        drawNormal_ = orbit::smoothNormal(drawNormal_, probe_.orbit.normal, eng.paused() ? 0.0 : (double)dt);
        closeness_ = align_.reason == orbit::Refusal::TooClose || align_.reason == orbit::Refusal::TooFar
                   ? 1.0 : orbit::alignmentCloseness(probe_.align, orbit::widened(tol_, kColourSpan));
        if (!was) LOG_D("orbit", "guide on: %s, altitude %.0f (shows within %.0f)", guideName_.c_str(), probe_.altitude, orbit::guideRange(probe_.radius, guideRange_, guideMinRange_));
        if (planeFallback_ != loggedFallback_) {
            loggedFallback_ = planeFallback_;
            LOG_D("orbit", "guide plane: %s", planeFallback_ ? "radial approach - using the ship's nose/right" : "from the velocity");
        }
    }

    void engage(engine::Engine& eng) {
        auto* ship = eng.services.get<ship::IShip>();
        auto* sys = eng.services.get<world::IStarSystem>();
        if (!ship || !sys) { LOG_I("orbit", "cannot lock: no %s", ship ? "star system" : "ship"); return; }
        const auto& st = ship->status();
        if (!st.alive) { LOG_I("orbit", "cannot lock: ship destroyed"); return; }
        if (st.warping) { LOG_I("orbit", "cannot lock while warping"); return; }
        if (auto* dock = eng.services.get<ship::IDocking>()) if (dock->busy()) { LOG_I("orbit", "cannot lock while docking or docked (press G to undock)"); return; }
        Probe pr;
        bool fallback = planeFallback_;                                  // same plane as the guide shows (a copy: the guide's state is not stepped twice)
        if (!probe(eng, pr, fallback)) { LOG_I("orbit", "cannot lock: no bodies"); return; }
        const auto& bodies = sys->bodies();
        const auto& b = bodies[pr.index];
        if (!orbit::inEngageRange(pr.altitude, b.radius, range_, minAlt_)) {
            LOG_I("orbit", "cannot lock: %s is %.0f units above the surface (range %.0f to %.0f)", b.name.c_str(), pr.altitude, (double)minAlt_, range_ * b.radius);
            eng.events.emit(ship::OrbitLockRefused{b.name, orbit::refusalText(pr.align)});
            return;
        }
        // aligned now, or still inside the latched (widened) tolerances the guide is showing green for
        bool latched = latch_.aligned && guideActive_ && probe_.index == pr.index && pr.wide.aligned();
        if (requireAlign_ && !pr.align.aligned() && !latched) {
            std::string why = orbit::refusalText(pr.align);
            LOG_I("orbit", "cannot lock to %s: %s", b.name.c_str(), why.c_str());
            eng.events.emit(ship::OrbitLockRefused{b.name, why});
            return;
        }
        orbit_ = pr.orbit;
        relVel0_ = pr.relVel;
        off_ = pr.rel;
        lockRadius_ = orbit_.radius;
        floorRadius_ = (double)b.radius + minAlt_;
        settleSeconds_used_ = requireAlign_ ? orbit::settleSecondsFor(settleSeconds_, pr.align, tol_) : 0.0;   // the old snap (require_alignment false) stays a snap
        settleLog_ = 0;
        body_ = pr.index; name_ = b.name; elapsed_ = 0; logTimer_ = 0;
        locked_ = true;
        guideActive_ = false;
        LOG_I("orbit", "LOCKED to %s: radius %.1f (altitude %.1f), circular speed %.1f m/s, period %.0f s, entry heading %.1f deg speed %+.1f m/s, settling %.2f s%s", name_.c_str(), orbit_.radius, pr.altitude, orbit_.speed,
              orbit_.omega > 0 ? 2.0 * 3.14159265358979 / orbit_.omega : 0.0, pr.align.headingDeg, pr.align.speedError, settleSeconds_used_, fallback ? " (radial-approach plane)" : "");
        eng.events.emit(ship::OrbitLockChanged{true, name_});
    }

    // ---- the guide ring (render pass) ----
    void drawGuide(core::RenderEngine& r) {
        if (!guideActive_ || !eng_) return;
        auto* sys = eng_->services.get<world::IStarSystem>();
        if (!sys || probe_.index < 0 || probe_.index >= (int)sys->bodies().size()) return;
        float m[16];
        for (int i = 0; i < 16; i++) m[i] = r.camera.view[i];
        world::Vec3d cam{-((double)m[0] * m[12] + (double)m[1] * m[13] + (double)m[2] * m[14]),
                         -((double)m[4] * m[12] + (double)m[5] * m[13] + (double)m[6] * m[14]),
                         -((double)m[8] * m[12] + (double)m[9] * m[13] + (double)m[10] * m[14])};
        m[12] = m[13] = m[14] = 0;

        world::Vec3d center = sys->positionAt(probe_.index);
        orbit::Orbit o = probe_.orbit;
        o.rel0 = orbit::sub(probe_.pos, center);                       // the body moved a little since the update
        o.radius = orbit::length(o.rel0);
        if (orbit::length(drawNormal_) > 0.5) o.normal = drawNormal_;
        o.rel0 = orbit::sub(o.rel0, orbit::mul(o.normal, orbit::dot(o.rel0, o.normal)));   // the ship's point in the drawn plane
        // vertices at fixed angles in the plane (not from the ship): they stay put as the ship moves round, nothing crawls
        orbit::stableCirclePoints(center, o.normal, o.radius, segments_, ring_);
        const world::Vec3d ref = orbit::planeReference(o.normal);
        const double shipAngle = orbit::planeAngle(o.normal, ref, o.rel0), step = 2.0 * 3.14159265358979 / segments_;
        const orbit::Rgb col = orbit::guideColour(closeness_);
        const float cr = col.r, cg = col.g, cb = col.b, green = (float)orbit::smoothstep01(1.0 - closeness_);
        const double ahead = 3.14159265358979 / 2.0;
        // calm at range: dimmer the farther the ship is from the ring's centre point of view (fades over 4 orbit radii)
        double fadeDist = std::max(1.0, 4.0 * o.radius);
        const int n = (int)ring_.size();
        for (int i = 0; i < n; i++) {
            world::Vec3d d = orbit::sub(ring_[(size_t)i], cam);
            verts_[(size_t)i * 3] = (float)d.x; verts_[(size_t)i * 3 + 1] = (float)d.y; verts_[(size_t)i * 3 + 2] = (float)d.z;
            float dist = (float)orbit::length(d);
            float fade = std::clamp(1.2f - dist / (float)fadeDist, 0.12f, 1.0f);
            float base = 0.32f + (0.55f - 0.32f) * green;
            double ah = orbit::aheadAngle(step * (i % segments_), shipAngle);
            float a = ah <= ahead ? (0.95f - 0.45f * (float)(ah / ahead)) : base;   // the arc ahead: brightest at the ship, still clear at its far end
            cols_[(size_t)i * 4] = cr; cols_[(size_t)i * 4 + 1] = cg; cols_[(size_t)i * 4 + 2] = cb; cols_[(size_t)i * 4 + 3] = a * fade;
        }

        // ticks every 30 degrees (only at 96+ segments: not on Low) and the two short markers at the ship
        int nl = 0;
        auto addLine = [&](const world::Vec3d& a, const world::Vec3d& b, float r_, float g_, float b_, float al) {
            if (nl + 2 > kMaxLineVerts) return;
            world::Vec3d da = orbit::sub(a, cam), db = orbit::sub(b, cam);
            float* v = &lineV_[(size_t)nl * 3]; float* c = &lineC_[(size_t)nl * 4];
            v[0] = (float)da.x; v[1] = (float)da.y; v[2] = (float)da.z; v[3] = (float)db.x; v[4] = (float)db.y; v[5] = (float)db.z;
            for (int k = 0; k < 2; k++) { c[k * 4] = r_; c[k * 4 + 1] = g_; c[k * 4 + 2] = b_; c[k * 4 + 3] = al; }
            nl += 2;
        };
        if (segments_ >= 96) {
            double tick = std::clamp(o.radius * 0.025, 4.0, 80.0);
            for (int k = 0; k < 12; k++) {
                world::Vec3d rel = orbit::rotateAbout(orbit::mul(ref, o.radius), o.normal, k * 3.14159265358979 / 6.0);
                world::Vec3d u = orbit::normalized(rel);
                addLine(orbit::add(center, orbit::add(rel, orbit::mul(u, -tick))), orbit::add(center, orbit::add(rel, orbit::mul(u, tick))), cr, cg, cb, 0.55f);
            }
        }
        double len = std::clamp(o.radius * 0.08, 15.0, 150.0);
        world::Vec3d tang = orbit::orbitTangent(o.normal, o.rel0);
        addLine(probe_.pos, orbit::add(probe_.pos, orbit::mul(tang, len)), cr, cg, cb, 1.0f);                           // where the orbit would take you
        double rs = orbit::length(probe_.relVel);
        if (rs > 1.0) addLine(probe_.pos, orbit::add(probe_.pos, orbit::mul(probe_.relVel, len / rs)), 1.0f, 1.0f, 1.0f, 0.85f);   // where you are actually going

        glLoadMatrixf(m);
        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_LINE_BIT | GL_CURRENT_BIT);
        glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
        glLineWidth(1.5f);
        glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, verts_.data());
        glColorPointer(4, GL_FLOAT, 0, cols_.data());
        glDrawArrays(GL_LINE_STRIP, 0, n);                                                     // draw call 1: the ring
        if (nl > 0) {
            glVertexPointer(3, GL_FLOAT, 0, lineV_);
            glColorPointer(4, GL_FLOAT, 0, lineC_);
            glDrawArrays(GL_LINES, 0, nl);                                                     // draw call 2: ticks + the two markers
        }
        glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_VERTEX_ARRAY);
        glPopAttrib();
    }

    // ---- the HUD block: top right, out of the way of the banner stack at the top centre (the HUD module is not touched) ----
    void drawBlock(core::UIHandler& ui) {
        if (!guideActive_) return;
        double now = eng_->time();
        if (now - textAt_ >= 0.1 || textAt_ > now) {          // numbers change every frame: refresh the strings 10x a second
            textAt_ = now;
            std::string l1 = orbit::guideLine1(guideName_, align_), l2 = orbit::guideLine2(align_);
            if (l1 != line1_) line1_ = std::move(l1);
            if (l2 != line2_) line2_ = std::move(l2);
        }
        const int W = ui.width(), H = ui.height();
        float s = std::clamp(std::min(W / 1280.0f, H / 720.0f), 0.6f, 3.0f);
        int f1 = (int)std::round(15 * s), f2 = (int)std::round(13 * s);
        float w = (float)std::max(ui.textWidth(line1_, f1), ui.textWidth(line2_, f2)) + 32 * s, h = 58 * s;
        float x = W - 16 * s - w, y = 16 * s;
        ui.glass(x, y, w, h, 0.9f);
        core::Color c1 = ui.theme.accent;
        const orbit::Rgb col = orbit::guideColour(closeness_);
        core::Color c2{col.r, col.g, col.b, 1.0f};
        ui.text(x + 16 * s, y + 8 * s, line1_, f1, c1);
        ui.text(x + 16 * s, y + 8 * s + f1 * 1.5f, line2_, f2, c2);
    }

    void release(engine::Engine& eng, const char* why) {
        locked_ = false;
        LOG_I("orbit", "RELEASED from %s (%s)", name_.c_str(), why);
        eng.events.emit(ship::OrbitLockChanged{false, name_});
    }

    static constexpr int kMaxLineVerts = 32;
    engine::Engine* eng_ = nullptr;
    core::RenderEngine* render_ = nullptr;
    core::UIHandler* ui_ = nullptr;
    core::IInput* input_ = nullptr;
    // guide state
    bool showGuide_ = true, requireAlign_ = true, guideActive_ = false;
    float guideRange_ = 6.0f, guideMinRange_ = 2500.0f, settleSeconds_ = 1.5f, settleLog_ = 0;
    double settleSeconds_used_ = 0, textAt_ = -1;
    int segments_ = 96;
    // the ring's colour spans 0..kColourSpan x the lock tolerances (aligned sits in the green third; far off is full amber)
    static constexpr double kColourSpan = 3.0;
    world::Vec3d drawNormal_{};
    int drawBody_ = -1;
    double closeness_ = 1.0;
    orbit::Tolerances tol_;
    orbit::Alignment align_;
    orbit::AlignLatch latch_;
    bool planeFallback_ = false, loggedFallback_ = false, prevValid_ = false;
    int fallbackBody_ = -1;
    Probe probe_;
    std::string guideName_, line1_, line2_;
    world::Vec3d relVel0_{}, off_{};
    double lockRadius_ = 0, floorRadius_ = 0;
    std::vector<orbit::Candidate> cand_;
    std::vector<world::Vec3d> ring_;
    std::vector<float> verts_, cols_;
    float lineV_[kMaxLineVerts * 3] = {}, lineC_[kMaxLineVerts * 4] = {};
    bool locked_ = false, releaseOnThrust_ = true;
    float gravity_ = 60.0f, range_ = 3.0f, minAlt_ = 20.0f, logTimer_ = 0;
    long autoOn_ = -1, autoOff_ = -1;
    int body_ = -1;
    double elapsed_ = 0;
    std::string name_;
    orbit::Orbit orbit_;
    std::vector<world::Vec3d> prevPos_, bodyVel_;
    static constexpr double kMaxBodySpeed = 20000.0;   // faster than any orbit (even at world.time_scale 100): a teleport, not motion
};

REGISTER_MODULE(OrbitLock);
