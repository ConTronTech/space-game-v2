// combat/weapons - a physical blaster (projectiles that inherit the ship's velocity, have finite speed and life, heat up the gun and kick the
// ship back), a mining beam, and guided missiles with lock-on (finite fuel / thrust / turn rate, proportional navigation, area damage). Weapons are data (data/weapons.json). Rules in weapons_rules.h, design and tunables in docs/COMBAT.md.
#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "combat/weapons/weapons_api.h"
#include "combat/weapons/weapons_data.h"
#include "combat/weapons/weapons_rules.h"
#include "combat/weapons/missile_rules.h"
#include "core/save_system/save_api.h"
#include "core/audio/audio_api.h"
#include "core/camera/camera_api.h"
#include "core/data_registry/data_api.h"
#include "core/input_handler/input_api.h"
#include "core/render_engine/render_engine.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "fx/particles/particles_api.h"
#include "ship/docking/docking_api.h"
#include "ship/ship_core/ship_api.h"
#include "world/asteroids/asteroids_api.h"
#include "world/star_system/star_system_api.h"
#include "world/stations/stations_api.h"

class Weapons : public engine::Module, public combat::ICombat, public combat::IAmmo, public core::ISaveable {
public:
    const char* name() const override { return "combat/weapons"; }
    std::vector<std::string> dependencies() const override { return {"core/input_handler"}; }
    // run after the ship, the asteroids, the stations and docking (their state must be this step's)
    std::vector<std::string> optionalDependencies() const override {
        return {"ship/ship_core", "ship/fake_ship", "core/render_engine", "world/asteroids", "world/star_system", "world/stations", "fx/particles", "core/audio",
                "ship/docking", "ship/warp_drive", "core/data_registry", "core/camera", "core/save_system"};
    }

    bool init(engine::Engine& eng) override {
        auto& c = eng.config;
        if (!c.get("combat.enabled", true, "weapons (blaster and mining beam)")) return true;
        int cap = std::clamp(c.get("combat.max_projectiles", 128, "most blaster bolts alive at once (0 = no projectiles)"), 0, 4096);
        recoilScale_ = std::max(0.0f, c.get("combat.recoil_scale", 1.0f, "ship recoil multiplier (0 = no recoil; the ship has no mass model yet)"));
        beamParticles_ = std::max(0.0f, c.get("combat.beam_particles", 1.0f, "rate multiplier of the sparks and debris at the mining beam's contact point"));
        cacheRadius_ = c.get("combat.cache_radius", 3000.0f, "asteroids within this distance of the ship are tested by bolts and the beam, units");
        cacheHz_ = std::max(0.5f, c.get("combat.cache_hz", 4.0f, "how often that nearby-asteroid list is refreshed"));
        weapons_ = combat::defaultWeapons();
        if (auto* data = eng.services.get<core::IData>()) {
            auto ids = data->ids("weapons");
            if (!ids.empty()) {
                auto defs = combat::defaultWeapons();
                std::vector<combat::WeaponDef> loaded;
                for (size_t i = 0; i < ids.size(); i++) loaded.push_back(combat::weaponFromJson(ids[i], data->get("weapons", ids[i]), i < defs.size() ? defs[i] : combat::WeaponDef{}));
                weapons_ = loaded;
            }
        }
        for (auto& w : weapons_) combat::sanitize(w);
        heat_.assign(weapons_.size(), combat::HeatState{});
        pool_.init(cap);
        // ---- missiles and lock-on ----
        int mcap = std::clamp(c.get("combat.max_missiles_alive", 8, "most missiles in flight at once (0 = missiles cannot be launched)"), 0, 64);
        maxMissiles_ = std::clamp(c.get("combat.max_missiles", 12, "missile rack size: a Missile Pack that does not fit is refused"), 0, 999);
        missiles_ = std::clamp(c.get("combat.start_missiles", 0, "missiles in the rack at the start of a new game"), 0, maxMissiles_);
        lockParams_.range = std::max(1.0f, c.get("combat.lock_range", 2500.0f, "lock-on range, units (T key)"));
        lockParams_.coneDeg = std::clamp(c.get("combat.lock_cone_deg", 12.0f, "lock-on cone half angle around the nose, degrees; acquiring must stay inside it"), 0.5f, 90.0f);
        lockParams_.keepConeDeg = std::max(lockParams_.coneDeg, (double)c.get("combat.lock_keep_cone_deg", 30.0f, "a finished lock is dropped when the target leaves this wider cone, degrees"));
        lockParams_.lockTime = std::max(0.0f, c.get("combat.lock_time", 1.0f, "seconds the target must stay in the cone to lock"));
        std::string gm = eng.flagValue("give-missiles");
        if (!gm.empty()) { missiles_ = std::clamp(std::atoi(gm.c_str()), 0, maxMissiles_); LOG_W("combat", "--give-missiles: %d missiles in the rack (test flag)", missiles_); }
        std::string al = eng.flagValue("auto-lock");
        autoLock_ = al.empty() ? -1 : std::atol(al.c_str());
        mpool_.init(mcap);
        lockCands_.reserve(256); ranked_.reserve(256);
        lines_.assign((size_t)(cap + mcap + 16) * 2 * 3, 0); lineCol_.assign((size_t)(cap + mcap + 16) * 2 * 4, 0);
        heads_.assign((size_t)(cap + mcap) * 4 * 3, 0); headCol_.assign((size_t)(cap + mcap) * 4 * 4, 0);
        near_.reserve(512);
        parseAutoFire(eng.flagValue("auto-fire"));
        std::string aw = eng.flagValue("auto-weapon");
        if (!aw.empty()) sel_ = std::clamp(std::atoi(aw.c_str()) - 1, 0, (int)weapons_.size() - 1);
        input_ = &eng.services.require<core::IInput>();
        eng_ = &eng;
        rng_ = combat::Rng(0xb1a57e5u);
        eng.services.provide<combat::ICombat>(this);
        eng.services.provide<combat::IAmmo>(this);
        if ((saves_ = eng.services.get<core::ISaveSystem>())) saves_->registerSaveable(this);
        if (auto* r = eng.services.get<core::RenderEngine>()) {
            render_ = r;
            r->addPass("combat/weapons", 75, [this](core::RenderEngine& rr) { draw(rr); });   // after the particles (70), before the cockpit (800)
        }
        active_ = true;
        LOG_I("combat", "%zu weapons, %d bolts max, %d missiles in flight max, rack %d / %d; selected: %s", weapons_.size(), cap, mcap, missiles_, maxMissiles_,
              weapons_[sel_].name.c_str());
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        if (!active_) return;
        if (render_) render_->removePass("combat/weapons");
        if (saves_) saves_->unregisterSaveable(this);
        eng.services.withdraw<combat::IAmmo>();
        eng.services.withdraw<combat::ICombat>();
    }

    // ---- combat::ICombat ----
    int weaponCount() const override { return (int)weapons_.size(); }
    int selected() const override { return sel_; }
    std::string weaponName(int i) const override { return i >= 0 && i < (int)weapons_.size() ? weapons_[i].name : std::string(); }
    float heat(int i) const override { return i >= 0 && i < (int)heat_.size() ? heat_[i].heat : 0.0f; }
    bool overheated(int i) const override { return i >= 0 && i < (int)heat_.size() && heat_[i].overheated; }
    bool firing() const override { return firing_; }
    float hitMarkerAge() const override { return eng_ ? (float)std::min(1.0e6, eng_->time() - lastHit_) : 1.0e6f; }
    int ammo(int i) const override { return i >= 0 && i < (int)weapons_.size() && weapons_[i].kind == combat::Kind::Missile ? missiles_ : -1; }
    bool noAmmo() const override { return !weapons_.empty() && weapons_[sel_].kind == combat::Kind::Missile && missiles_ <= 0; }
    int missileCount() const override { return mpool_.n; }
    combat::LockStateId lockState() const override { return (combat::LockStateId)(int)lock_.state; }
    std::string lockTargetName() const override { return lockName_; }
    float lockTargetDistance() const override { return lockDist_; }
    float lockTargetAngle() const override { return lockAngle_; }
    float lockProgress() const override {
        if (lock_.state == combat::LockState::Locked) return 1.0f;
        if (lock_.state != combat::LockState::Acquiring) return 0.0f;
        return lockParams_.lockTime > 0 ? std::min(1.0f, lock_.timer / lockParams_.lockTime) : 1.0f;
    }

    // ---- combat::IAmmo ----
    int addMissiles(int n) override {
        int got = combat::acceptMissiles(missiles_, maxMissiles_, n);
        missiles_ += got;
        LOG_I("combat", got > 0 ? "+%d missiles (rack %d / %d)" : "missile rack full: %d refused (rack %d / %d)", got > 0 ? got : n, missiles_, maxMissiles_);
        return got;
    }
    int missiles() const override { return missiles_; }
    int maxMissiles() const override { return maxMissiles_; }

    // ---- saving: the rack and the selected weapon ----
    const char* saveId() const override { return "combat/weapons"; }
    engine::Json save() const override { return combat::weaponsSave(missiles_, sel_); }
    void load(const engine::Json& j) override {
        int before = sel_;
        combat::weaponsLoad(j, maxMissiles_, (int)weapons_.size(), missiles_, sel_);
        mpool_.n = 0; combat::clearLock(lock_); lockName_.clear();
        LOG_I("combat", "loaded: %d missiles, weapon %d (%s)", missiles_, sel_ + 1, weapons_[sel_].name.c_str());
        if (sel_ != before && eng_) eng_->events.emit(combat::WeaponChanged{sel_, weapons_[sel_].name});
    }

    void onUpdate(engine::Engine& eng, float) override {
        if (!active_ || eng.paused()) return;
        int want = sel_;
        if (input_->pressed("weapon_1")) want = 0;
        if (input_->pressed("weapon_2")) want = 1;
        if (input_->pressed("weapon_3")) want = 2;
        if (input_->pressed("lock_target")) lockPress_ = true;             // handled in the fixed step (needs this step's pose)
        if (input_->pressed("lock_clear")) lockClear_ = true;
        if (input_->pressed("weapon_next")) want = (sel_ + 1) % (int)weapons_.size();
        want = std::clamp(want, 0, (int)weapons_.size() - 1);
        if (want != sel_) {
            sel_ = want;
            LOG_I("combat", "weapon %d selected: %s", sel_ + 1, weapons_[sel_].name.c_str());
            eng.events.emit(combat::WeaponChanged{sel_, weapons_[sel_].name});
        }
    }

    void onFixedUpdate(engine::Engine& eng, float dt) override {
        if (!active_) return;
        auto t0 = std::chrono::steady_clock::now();
        auto* ship = eng.services.get<ship::IShip>();
        auto* tf = eng.services.get<core::ITransformSource>();
        if (!ship || !tf) return;
        for (size_t i = 0; i < weapons_.size(); i++) combat::tickHeat(heat_[i], weapons_[i], dt);
        long f = (long)eng.frame();
        bool scripted = autoFire_ >= 0 && f >= autoFire_ && f < autoFire_ + std::max(1L, autoFireFrames_);
        firing_ = triggerHeld(eng, *ship, scripted);
        core::Pose ps = tf->transform(1.0f);                          // physics state (the current step), not an interpolation
        combat::Vec3d pos{ps.pos.x, ps.pos.y, ps.pos.z}, fwd{ps.fwd.x, ps.fwd.y, ps.fwd.z}, up{ps.up.x, ps.up.y, ps.up.z};
        auto v = ship->velocity();
        combat::Vec3d sv{v.x, v.y, v.z};

        refreshCache(eng, pos, dt);
        updateLockOn(eng, pos, fwd, dt, f);
        beamActive_ = false;
        const combat::WeaponDef& w = weapons_[sel_];
        combat::HeatState& hs = heat_[sel_];
        if (firing_) {
            if (w.kind == combat::Kind::Projectile) fireBlaster(eng, *ship, w, hs, pos, fwd, up, sv);
            else if (w.kind == combat::Kind::Missile) fireMissile(eng, w, hs, pos, fwd, up, sv);
            else fireBeam(eng, w, hs, pos, fwd, up, dt, scripted);
        } else { beamAccum_ = 0; noAmmoLogged_ = false; }
        advanceBolts(eng, dt);
        advanceMissiles(eng, dt);
        fireMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        steps_++;
        if (eng.time() - lastReport_ >= 1.0 && (firing_ || pool_.n > 0 || mpool_.n > 0 || lock_.state != combat::LockState::Idle)) {
            lastReport_ = eng.time();
            LOG_D("combat", "%s heat %.2f%s, %d bolts, %d missiles (rack %d), lock %s, speed %.2f m/s, CPU per step %.4f ms", w.name.c_str(), hs.heat,
                  hs.overheated ? " OVERHEATED" : "", pool_.n, mpool_.n, missiles_, combat::lockStateName(lock_.state), ship->status().speed, steps_ ? fireMs_ / steps_ : 0.0);
            fireMs_ = 0; steps_ = 0;
        }
    }

private:
    // --auto-fire=FRAME[,FRAMES]: hold the trigger for FRAMES engine frames starting at FRAME (default 1)
    void parseAutoFire(const std::string& v) {
        if (v.empty()) { autoFire_ = -1; return; }
        autoFire_ = std::atol(v.c_str());
        auto comma = v.find(',');
        autoFireFrames_ = comma == std::string::npos ? 1 : std::atol(v.c_str() + comma + 1);
    }

    bool triggerHeld(engine::Engine& eng, ship::IShip& ship, bool scripted) {
        const auto& st = ship.status();
        const char* blocked = !st.alive ? "ship destroyed" : st.warping ? "warp drive engaged" : nullptr;
        if (!blocked) if (auto* dock = eng.services.get<ship::IDocking>()) if (dock->busy()) blocked = "docked";
        if (blocked) {
            if (scripted && !loggedBlock_) { LOG_D("combat", "fire ignored: %s", blocked); loggedBlock_ = true; }   // dev flag only: the player's clicks are silently ignored
            return false;
        }
        loggedBlock_ = false;
        if (scripted) return true;                                    // dev flag: fire without a mouse
        if (!input_->down("fire")) return false;
        return SDL_GetRelativeMouseMode() == SDL_TRUE;                // free mouse (menu clicks, Tab): the same condition the ship uses for mouse look
    }

    // ---- the asteroids near the ship: refreshed a few times a second, tested every step ----
    struct Near { int id; combat::Vec3d pos; double radius; };
    void refreshCache(engine::Engine& eng, const combat::Vec3d& shipPos, float dt) {
        cacheTimer_ -= dt;
        if (cacheTimer_ > 0.0f) return;
        cacheTimer_ = 1.0f / cacheHz_;
        near_.clear();
        auto* ast = eng.services.get<world::IAsteroids>();
        if (!ast) return;
        ast->nearest({shipPos.x, shipPos.y, shipPos.z}, 256, idScratch_);           // nearest first; destroyed ones are not listed
        for (int id : idScratch_) {
            auto p = ast->position(id);
            if (combat::length(combat::sub({p.x, p.y, p.z}, shipPos)) > cacheRadius_) break;
            near_.push_back({id, {p.x, p.y, p.z}, (double)ast->radius(id)});
        }
    }

    void fireBlaster(engine::Engine& eng, ship::IShip& ship, const combat::WeaponDef& w, combat::HeatState& hs, const combat::Vec3d& pos, const combat::Vec3d& fwd,
                     const combat::Vec3d& up, const combat::Vec3d& sv) {
        bool tipped = false;
        if (!combat::fireShot(hs, w, tipped)) return;
        combat::Vec3d dir = combat::spreadDirection(rng_, fwd, w.spread);
        combat::Vec3d muzzle = combat::muzzlePosition(pos, fwd, up, w.muzzle);
        pool_.spawn(muzzle, combat::boltVelocity(sv, dir, w.speed), w.lifetime, w.damage, 0);
        if (recoilScale_ > 0.0f && w.recoil > 0.0f) {
            combat::Vec3d nv = combat::recoilVelocity(sv, dir, w.recoil, recoilScale_);
            ship.setVelocity({(float)nv.x, (float)nv.y, (float)nv.z});
        }
        fx::SpawnParticles m; m.kind = "muzzle"; m.position = muzzle; m.direction = dir; m.velocity = sv;
        eng.events.emit(m);
        if (auto* audio = eng.services.get<core::IAudio>()) if (audio->hasSound("blaster")) audio->play("blaster", 0.5f);
        if (tipped) onOverheat(eng);
    }

    void fireBeam(engine::Engine& eng, const combat::WeaponDef& w, combat::HeatState& hs, const combat::Vec3d& pos, const combat::Vec3d& fwd, const combat::Vec3d& up, float dt, bool) {
        bool tipped = false;
        if (!combat::fireBeam(hs, w, dt, tipped)) return;
        combat::Vec3d origin = combat::muzzlePosition(pos, fwd, up, w.muzzle);
        double best = w.range; int hit = -1;
        for (size_t k = 0; k < near_.size(); k++) {
            double d;
            if (combat::raySphere(origin, fwd, best, near_[k].pos, near_[k].radius, d) && d < best) { best = d; hit = (int)k; }
        }
        beamActive_ = true;
        beamStart_ = origin;
        beamEnd_ = combat::add(origin, combat::mul(fwd, best));
        beamHit_ = hit >= 0;
        if (hit >= 0) {
            lastHit_ = eng.time();
            auto* ast = eng.services.get<world::IAsteroids>();
            if (ast) ast->damage(near_[hit].id, w.beamDps * dt, {beamEnd_.x, beamEnd_.y, beamEnd_.z});
            beamAccum_ += 25.0f * beamParticles_ * dt;                   // sparks and debris at the contact, at a modest rate
            while (beamAccum_ >= 1.0f) {
                beamAccum_ -= 1.0f;
                combat::Vec3d n = combat::sub(origin, beamEnd_);
                fx::SpawnParticles s; s.kind = (++beamCount_ % 4 == 0) ? "debris" : "spark"; s.position = beamEnd_; s.direction = n; s.count = 2; s.size = 0.6f;
                s.velocity = {0, 0, 0};
                eng.events.emit(s);
            }
        }
        if (tipped) onOverheat(eng);
    }

    void onOverheat(engine::Engine& eng) {
        LOG_I("combat", "%s OVERHEATED (locked out %.1f s)", weapons_[sel_].name.c_str(), weapons_[sel_].lockoutSeconds);
        eng.events.emit(combat::Overheated{sel_, weapons_[sel_].name});
    }

    // ---- bolts: advance and sweep the segment against everything a bolt can hit ----
    void advanceBolts(engine::Engine& eng, float dt) {
        if (pool_.n == 0) return;
        auto* sys = eng.services.get<world::IStarSystem>();
        auto* stations = eng.services.get<world::IStations>();
        auto* ast = eng.services.get<world::IAsteroids>();
        for (int i = pool_.n - 1; i >= 0; i--) {
            pool_.life[i] -= dt;
            if (pool_.life[i] <= 0.0f) { pool_.remove(i); continue; }
            combat::Vec3d p0{pool_.px[i], pool_.py[i], pool_.pz[i]}, vel{pool_.vx[i], pool_.vy[i], pool_.vz[i]};
            combat::Vec3d p1 = combat::boltStep(p0, vel, dt);
            double stepLen = combat::length(combat::sub(p1, p0)), bestT = 2.0;
            const char* kind = nullptr; int id = -1; combat::Vec3d centre{};
            auto consider = [&](const combat::Vec3d& c, double r, const char* k, int ident) {
                if (combat::length(combat::sub(c, p0)) > stepLen + r + 1.0) return;              // cheap reject: too far to touch this step
                double t;
                if (combat::segmentSphere(p0, p1, c, r, t) && t < bestT) {
                    if (k[0] == 'a' && ast && !ast->alive(ident)) return;                  // the cached list is a few frames old: a rock destroyed since is not a target
                    bestT = t; kind = k; id = ident; centre = c;
                }
            };
            for (auto& a : near_) consider(a.pos, a.radius, "asteroid", a.id);
            if (sys) for (auto& b : sys->bodies())
                consider({b.position.x, b.position.y, b.position.z}, b.radius, b.kind == world::BodyKind::Sun ? "sun" : b.kind == world::BodyKind::Moon ? "moon" : "planet", b.id);
            if (stations) for (int s = 0; s < stations->count(); s++) {
                auto in = stations->info(s);
                consider({in.position.x, in.position.y, in.position.z}, in.half * 1.5, "station", s);
            }
            if (!kind) { pool_.px[i] = p1.x; pool_.py[i] = p1.y; pool_.pz[i] = p1.z; continue; }
            combat::Vec3d hit = combat::add(p0, combat::mul(combat::sub(p1, p0), bestT));
            float dmg = pool_.damage[i];
            bool isAsteroid = kind[0] == 'a';
            if (isAsteroid && ast) ast->damage(id, dmg, {hit.x, hit.y, hit.z});
            lastHit_ = eng.time();
            combat::Vec3d n = combat::sub(hit, centre);
            double nl = combat::length(n);
            n = nl > 1e-9 ? combat::mul(n, 1.0 / nl) : combat::Vec3d{0, 1, 0};
            fx::SpawnParticles s; s.kind = "spark"; s.position = hit; s.direction = n; s.count = 8; s.size = 0.8f;
            eng.events.emit(s);
            if (auto* audio = eng.services.get<core::IAudio>()) if (audio->hasSound("impact")) audio->play("impact", 0.25f);
            LOG_D("combat", "bolt hit %s %d for %.0f", kind, id, isAsteroid ? dmg : 0.0f);
            eng.events.emit(combat::ProjectileHit{kind, id, hit, isAsteroid ? dmg : 0.0f, pool_.shooter[i]});
            pool_.remove(i);
        }
    }

    // ---- lock-on: T picks / cycles, Y (or T on the only target) clears; the state machine runs every step ----
    void updateLockOn(engine::Engine& eng, const combat::Vec3d& pos, const combat::Vec3d& fwd, float dt, long frame) {
        auto* ast = eng.services.get<world::IAsteroids>();
        if (autoLock_ >= 0 && frame == autoLock_) { lockPress_ = true; LOG_W("combat", "--auto-lock: lock pressed at frame %ld (test flag)", frame); }
        if (lockClear_ && lock_.state != combat::LockState::Idle) { LOG_I("combat", "lock cleared"); combat::clearLock(lock_); }
        if (lockPress_ && ast) {
            lockCands_.clear();
            for (auto& a : near_) if (ast->alive(a.id)) lockCands_.push_back({a.id, a.pos, a.radius});
            combat::rankCandidates(pos, fwd, lockCands_, lockParams_, ranked_);
            int before = lock_.target;
            int t = combat::pressLock(lock_, lockCands_, ranked_);
            if (t < 0 && before >= 0) LOG_I("combat", "lock cleared (T on the same target)");
            else if (t < 0) {
                LOG_I("combat", "lock: no target inside the %.0f deg cone within %.0f units (%zu rocks nearby)", lockParams_.coneDeg, lockParams_.range, lockCands_.size());
                int shown = 0;
                for (auto& c : lockCands_) if (c.radius >= 9.0 && shown < 3) {
                    shown++;
                    LOG_D("combat", "big rock nearby: asteroid %d at %.1f %.1f %.1f, radius %.1f, %.0f units", c.id, c.pos.x, c.pos.y, c.pos.z, c.radius, combat::length(combat::sub(c.pos, pos)));
                }
                ast->nearest({pos.x, pos.y, pos.z}, 1, blastScratch_);  // for scripted scenarios: where to point the ship (key press only: O(count) is fine)
                if (!blastScratch_.empty()) {
                    int id = blastScratch_[0];
                    auto ap = ast->position(id);
                    combat::Vec3d apd{ap.x, ap.y, ap.z};
                    LOG_D("combat", "nearest rock: asteroid %d at %.1f %.1f %.1f, radius %.1f, %.0f units, %.1f deg off the nose", id, ap.x, ap.y, ap.z, ast->radius(id),
                          combat::length(combat::sub(apd, pos)), combat::angleDeg(fwd, combat::sub(apd, pos)));
                }
            }
            else if (t != before) LOG_I("combat", "lock: acquiring asteroid %d (%d candidates in the cone)", t, (int)ranked_.size());
        }
        lockPress_ = lockClear_ = false;
        combat::LockState was = lock_.state;
        bool alive = ast && lock_.target >= 0 && ast->alive(lock_.target);
        combat::Vec3d tp{};
        if (ast && lock_.target >= 0) { auto p = ast->position(lock_.target); tp = {p.x, p.y, p.z}; }
        combat::updateLock(lock_, lockParams_, dt, pos, fwd, alive, tp);
        if (lock_.target >= 0 && ast) {
            combat::Vec3d r = combat::sub(tp, pos);
            lockDist_ = (float)combat::length(r); lockAngle_ = (float)combat::angleDeg(fwd, r);
            lockPos_ = tp; lockRadius_ = ast->radius(lock_.target);
            if (lockNameId_ != lock_.target) {
                char b[96]; std::snprintf(b, sizeof b, "asteroid %d (%s)", lock_.target, ast->ore(lock_.target).c_str());
                lockName_ = b; lockNameId_ = lock_.target;
            }
        } else { lockName_.clear(); lockNameId_ = -1; lockDist_ = lockAngle_ = 0; }
        if (lock_.state != was) {
            if (lock_.state == combat::LockState::Lost) LOG_I("combat", "lock lost: %s (%s, %.0f units, %.1f deg)", lock_.why, lockName_.c_str(), lockDist_, lockAngle_);
            else if (lock_.state == combat::LockState::Locked) LOG_I("combat", "LOCKED %s at %.0f units, %.1f deg off the nose", lockName_.c_str(), lockDist_, lockAngle_);
            else LOG_D("combat", "lock %s", combat::lockStateName(lock_.state));
        }
        if (lock_.state == combat::LockState::Acquiring && eng.time() - lastLockLog_ >= 0.25) {
            lastLockLog_ = eng.time();
            LOG_D("combat", "acquiring %s: %.0f units, %.1f deg (cone %.0f), %.2f / %.2f s", lockName_.c_str(), lockDist_, lockAngle_, lockParams_.coneDeg, lock_.timer, lockParams_.lockTime);
        }
    }

    // ---- missiles: ammo, a slow rate of fire (the shot timer, no heat), launched with the ship's velocity plus a kick ----
    void fireMissile(engine::Engine& eng, const combat::WeaponDef& w, combat::HeatState& hs, const combat::Vec3d& pos, const combat::Vec3d& fwd, const combat::Vec3d& up,
                     const combat::Vec3d& sv) {
        if (!combat::canFire(hs)) return;
        if (missiles_ <= 0) {
            if (!noAmmoLogged_) { LOG_I("combat", "NO MISSILES: launch refused (craft a Missile Pack)"); noAmmoLogged_ = true; }
            return;
        }
        if (mpool_.n >= mpool_.capacity) {
            if (!noAmmoLogged_) { LOG_I("combat", "launch refused: %d missiles already in flight (combat.max_missiles_alive)", mpool_.n); noAmmoLogged_ = true; }
            return;
        }
        bool tipped = false;
        combat::fireShot(hs, w, tipped);
        combat::takeMissile(missiles_);
        combat::Vec3d muzzle = combat::muzzlePosition(pos, fwd, up, w.muzzle);
        combat::MissileState m = combat::launchMissile(muzzle, sv, fwd, w.missile);
        int tgt = lock_.state == combat::LockState::Locked ? lock_.target : -1;
        mpool_.spawn(m, tgt, 0);
        LOG_I("combat", "missile launched %s (rack %d / %d, %d in flight)", tgt >= 0 ? ("at " + lockName_).c_str() : "unguided (no lock)", missiles_, maxMissiles_, mpool_.n);
        fx::SpawnParticles e; e.kind = "muzzle"; e.position = muzzle; e.direction = fwd; e.velocity = sv; e.count = 14;
        eng.events.emit(e);
        if (auto* audio = eng.services.get<core::IAudio>()) {
            if (audio->hasSound("missile")) audio->play("missile", 0.6f);
            else if (audio->hasSound("blaster")) audio->play("blaster", 0.7f);
        }
    }

    const combat::WeaponDef* missileDef() const { for (auto& w : weapons_) if (w.kind == combat::Kind::Missile) return &w; return nullptr; }

    void advanceMissiles(engine::Engine& eng, float dt) {
        if (mpool_.n == 0) return;
        const combat::WeaponDef* wd = missileDef();
        if (!wd) { mpool_.n = 0; return; }
        const combat::MissileParams& mp = wd->missile;
        auto* sys = eng.services.get<world::IStarSystem>();
        auto* stations = eng.services.get<world::IStations>();
        auto* ast = eng.services.get<world::IAsteroids>();
        bool logNow = eng.time() - lastMissileLog_ >= 0.5;
        if (logNow) lastMissileLog_ = eng.time();
        for (int i = mpool_.n - 1; i >= 0; i--) {
            combat::MissileState m = mpool_.get(i);
            int tgt = mpool_.target[i];
            if (tgt >= 0 && !(ast && ast->alive(tgt))) { mpool_.target[i] = tgt = -1; LOG_D("combat", "missile %d: target gone, flying straight", i); }
            combat::Vec3d tp{};
            double tr = 0;
            if (tgt >= 0) { auto p = ast->position(tgt); tp = {p.x, p.y, p.z}; tr = ast->radius(tgt); }
            combat::Vec3d p0 = m.pos;
            combat::stepMissile(m, mp, tgt >= 0, tp, {0, 0, 0}, dt);          // asteroids are static; the guidance handles moving targets too
            combat::Vec3d p1 = m.pos;
            if (logNow) LOG_D("combat", "missile %d: pos %.0f %.0f %.0f, speed %.0f m/s, fuel %.1f s, life %.1f s%s", i, m.pos.x, m.pos.y, m.pos.z, combat::length(m.vel), m.fuel, m.life,
                              tgt >= 0 ? (", target " + std::to_string((int)combat::length(combat::sub(tp, m.pos))) + " units").c_str() : ", unguided");
            if (combat::isArmed(m, mp)) {
                double stepLen = combat::length(combat::sub(p1, p0)), bestT = 2.0;
                const char* kind = nullptr;
                auto consider = [&](const combat::Vec3d& c, double r, const char* k) {
                    if (combat::length(combat::sub(c, p0)) > stepLen + r + 1.0) return;
                    double t;
                    if (combat::segmentSphere(p0, p1, c, r, t) && t < bestT) { bestT = t; kind = k; }
                };
                for (auto& a : near_) if (!ast || ast->alive(a.id)) consider(a.pos, a.radius, "impact");
                if (tgt >= 0) consider(tp, tr + mp.fuseRadius, "proximity fuse");             // the target: also the proximity fuse
                if (sys) for (auto& b : sys->bodies()) consider({b.position.x, b.position.y, b.position.z}, b.radius, "impact (body)");
                if (stations) for (int s = 0; s < stations->count(); s++) { auto in = stations->info(s); consider({in.position.x, in.position.y, in.position.z}, in.half * 1.5, "impact (station)"); }
                if (kind) {
                    explode(eng, combat::add(p0, combat::mul(combat::sub(p1, p0), bestT)), mp, kind, mpool_.shooter[i]);
                    mpool_.remove(i);
                    continue;
                }
            }
            if (m.life <= 0.0f) { explode(eng, m.pos, mp, m.fuel > 0 ? "lifetime" : "fuel and lifetime out", mpool_.shooter[i]); mpool_.remove(i); continue; }
            mpool_.set(i, m);
        }
    }

    // Area damage to every rock whose surface is within the blast radius, linear falloff; one ProjectileHit per damaged rock.
    void explode(engine::Engine& eng, const combat::Vec3d& at, const combat::MissileParams& mp, const char* reason, int shooter) {
        int damaged = 0;
        float total = 0;
        if (auto* ast = eng.services.get<world::IAsteroids>()) {
            ast->nearest({at.x, at.y, at.z}, 32, blastScratch_);
            for (int id : blastScratch_) {
                auto p = ast->position(id);
                double sd = combat::length(combat::sub({p.x, p.y, p.z}, at)) - ast->radius(id);
                float dmg = combat::blastDamage(sd, mp);
                if (dmg <= 0.0f) continue;
                bool killed = ast->damage(id, dmg, {at.x, at.y, at.z});
                eng.events.emit(combat::ProjectileHit{"asteroid", id, at, dmg, shooter});
                LOG_I("combat", "missile blast: asteroid %d takes %.0f (surface %.0f units away)%s", id, dmg, std::max(0.0, sd), killed ? " - DESTROYED" : "");
                damaged++; total += dmg;
            }
        }
        if (damaged) lastHit_ = eng.time();
        LOG_I("combat", "missile exploded (%s) at %.0f %.0f %.0f: %d asteroids damaged, %.0f total", reason, at.x, at.y, at.z, damaged, total);
        eng.events.emit(combat::MissileExploded{at, (float)mp.blastRadius, reason, shooter});
        for (const char* k : {"explosion", "debris", "spark"}) {
            fx::SpawnParticles s; s.kind = k; s.position = at; s.direction = {0, 1, 0}; s.velocity = {0, 0, 0};
            if (k[0] == 's') { s.count = 24; s.size = 1.5f; }
            if (k[0] == 'd') { s.count = 12; s.size = 1.5f; }
            eng.events.emit(s);
        }
        if (auto* audio = eng.services.get<core::IAudio>()) {
            if (audio->hasSound("explosion")) audio->play("explosion", 0.8f);
            else if (audio->hasSound("impact")) audio->play("impact", 0.8f);
        }
    }

    // ---- drawing: bolts as short additive streaks, the beam as a thin line; one glDrawArrays ----
    void draw(core::RenderEngine& r) {
        bool marker = lock_.target >= 0 && (lock_.state == combat::LockState::Locked || (lock_.state == combat::LockState::Acquiring && std::fmod(eng_->time() * 4.0, 1.0) < 0.6));
        if (pool_.n == 0 && mpool_.n == 0 && !beamActive_ && !marker) return;
        float m[16];
        for (int i = 0; i < 16; i++) m[i] = r.camera.view[i];
        combat::Vec3d cam{-((double)m[0] * m[12] + (double)m[1] * m[13] + (double)m[2] * m[14]),
                          -((double)m[4] * m[12] + (double)m[5] * m[13] + (double)m[6] * m[14]),
                          -((double)m[8] * m[12] + (double)m[9] * m[13] + (double)m[10] * m[14])};
        m[12] = m[13] = m[14] = 0;
        glLoadMatrixf(m);
        int verts = 0, headVerts = 0;
        const float rx = m[0], ry = m[4], rz = m[8], ux = m[1], uy = m[5], uz = m[9];       // camera right / up in world axes
        const combat::WeaponDef& w = weapons_[0];
        auto line = [&](const combat::Vec3d& a, const combat::Vec3d& b, const float* col, float a0, float a1) {
            float* p = &lines_[(size_t)verts * 3]; float* c = &lineCol_[(size_t)verts * 4];
            p[0] = (float)(a.x - cam.x); p[1] = (float)(a.y - cam.y); p[2] = (float)(a.z - cam.z);
            p[3] = (float)(b.x - cam.x); p[4] = (float)(b.y - cam.y); p[5] = (float)(b.z - cam.z);
            for (int k = 0; k < 3; k++) { c[k] = col[k]; c[4 + k] = col[k]; }
            c[3] = a0; c[7] = a1;
            verts += 2;
        };
        for (int i = 0; i < pool_.n && verts + 2 <= (int)(lines_.size() / 3); i++) {
            combat::Vec3d p{pool_.px[i], pool_.py[i], pool_.pz[i]}, v{pool_.vx[i], pool_.vy[i], pool_.vz[i]};
            double vl = combat::length(v);
            if (vl < 1e-6) continue;
            line(p, combat::sub(p, combat::mul(v, 12.0 / vl)), w.colour, 1.0f, 0.0f);         // a 12-unit tail fading behind the bolt
            // seen head-on (chase view: the camera looks along the bolt's path) a streak shrinks to nothing, so the head is also a small bright square
            // that stays at least ~2 pixels big at any distance
            double dx = p.x - cam.x, dy = p.y - cam.y, dz = p.z - cam.z, dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            float h = (float)std::max(0.35, dist * 0.006);
            if ((headVerts + 4) * 3 <= (int)heads_.size()) {
                float* q = &heads_[(size_t)headVerts * 3]; float* c = &headCol_[(size_t)headVerts * 4];
                float cx = (float)dx, cy = (float)dy, cz = (float)dz;
                const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
                for (int k = 0; k < 4; k++) {
                    q[k * 3] = cx + (rx * sx[k] + ux * sy[k]) * h; q[k * 3 + 1] = cy + (ry * sx[k] + uy * sy[k]) * h; q[k * 3 + 2] = cz + (rz * sx[k] + uz * sy[k]) * h;
                    c[k * 4] = 0.7f + 0.3f * w.colour[0]; c[k * 4 + 1] = 0.7f + 0.3f * w.colour[1]; c[k * 4 + 2] = 0.7f + 0.3f * w.colour[2]; c[k * 4 + 3] = 1.0f;
                }
                headVerts += 4;
            }
        }
        if (beamActive_ && verts + 2 <= (int)(lines_.size() / 3)) line(beamStart_, beamEnd_, weapons_[sel_].colour, 0.9f, beamHit_ ? 0.9f : 0.35f);
        // missiles: a longer streak along the flight path and a bigger, hotter head (same batches as the bolts)
        if (const combat::WeaponDef* md = missileDef()) {
            const float* mc = md->colour;
            for (int i = 0; i < mpool_.n && verts + 2 <= (int)(lines_.size() / 3); i++) {
                combat::Vec3d p{mpool_.px[i], mpool_.py[i], mpool_.pz[i]}, h{mpool_.hx[i], mpool_.hy[i], mpool_.hz[i]};
                bool burning = mpool_.fuel[i] > 0;
                line(p, combat::sub(p, combat::mul(h, burning ? 25.0 : 8.0)), mc, 1.0f, 0.0f);
                double dx = p.x - cam.x, dy = p.y - cam.y, dz = p.z - cam.z, dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                float hs = (float)std::max(0.6, dist * 0.008);
                if ((headVerts + 4) * 3 > (int)heads_.size()) break;
                float* q = &heads_[(size_t)headVerts * 3]; float* c = &headCol_[(size_t)headVerts * 4];
                const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
                for (int k = 0; k < 4; k++) {
                    q[k * 3] = (float)dx + (rx * sx[k] + ux * sy[k]) * hs; q[k * 3 + 1] = (float)dy + (ry * sx[k] + uy * sy[k]) * hs; q[k * 3 + 2] = (float)dz + (rz * sx[k] + uz * sy[k]) * hs;
                    c[k * 4] = 1.0f; c[k * 4 + 1] = 0.6f + 0.4f * mc[1]; c[k * 4 + 2] = 0.4f + 0.3f * mc[2]; c[k * 4 + 3] = 1.0f;
                }
                headVerts += 4;
            }
        }
        // the lock marker: a diamond around the target (blinking amber while acquiring, solid red when locked) plus four inner ticks when locked
        if (marker && verts + 16 <= (int)(lines_.size() / 3)) {
            double dx = lockPos_.x - cam.x, dy = lockPos_.y - cam.y, dz = lockPos_.z - cam.z, dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            double s = std::max((double)lockRadius_ * 1.6, dist * 0.025);
            combat::Vec3d R{rx * s, ry * s, rz * s}, U{ux * s, uy * s, uz * s};
            combat::Vec3d top = combat::add(lockPos_, U), bot = combat::sub(lockPos_, U), rgt = combat::add(lockPos_, R), lft = combat::sub(lockPos_, R);
            bool locked = lock_.state == combat::LockState::Locked;
            const float amber[3] = {1.0f, 0.75f, 0.2f}, red[3] = {1.0f, 0.25f, 0.2f};
            const float* col = locked ? red : amber;
            line(top, rgt, col, 0.95f, 0.95f); line(rgt, bot, col, 0.95f, 0.95f); line(bot, lft, col, 0.95f, 0.95f); line(lft, top, col, 0.95f, 0.95f);
            if (locked) {
                line(top, combat::add(lockPos_, combat::mul(U, 0.6)), col, 0.95f, 0.95f); line(bot, combat::sub(lockPos_, combat::mul(U, 0.6)), col, 0.95f, 0.95f);
                line(rgt, combat::add(lockPos_, combat::mul(R, 0.6)), col, 0.95f, 0.95f); line(lft, combat::sub(lockPos_, combat::mul(R, 0.6)), col, 0.95f, 0.95f);
            }
        }
        if (verts == 0 && headVerts == 0) return;
        glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_LINE_BIT);
        glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D);
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glLineWidth(2.0f);
        glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, lines_.data());
        glColorPointer(4, GL_FLOAT, 0, lineCol_.data());
        if (verts > 0) glDrawArrays(GL_LINES, 0, verts);
        if (headVerts > 0) {
            glVertexPointer(3, GL_FLOAT, 0, heads_.data());
            glColorPointer(4, GL_FLOAT, 0, headCol_.data());
            glDrawArrays(GL_QUADS, 0, headVerts);
        }
        glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_VERTEX_ARRAY);
        glPopAttrib();
    }

    engine::Engine* eng_ = nullptr;
    core::ISaveSystem* saves_ = nullptr;
    // missiles and lock-on
    int missiles_ = 0, maxMissiles_ = 12, lockNameId_ = -1;
    long autoLock_ = -1;
    bool lockPress_ = false, lockClear_ = false, noAmmoLogged_ = false;
    float lockDist_ = 0, lockAngle_ = 0, lockRadius_ = 0;
    double lastLockLog_ = -10, lastMissileLog_ = -10;
    combat::Vec3d lockPos_;
    std::string lockName_;
    combat::MissilePool mpool_;
    combat::Lock lock_;
    combat::LockParams lockParams_;
    std::vector<combat::LockCandidate> lockCands_;
    std::vector<int> ranked_, blastScratch_;
    core::RenderEngine* render_ = nullptr;
    core::IInput* input_ = nullptr;
    bool loggedBlock_ = false, active_ = false, firing_ = false, beamActive_ = false, beamHit_ = false;
    long autoFire_ = -1, autoFireFrames_ = 1;
    int sel_ = 0, beamCount_ = 0, steps_ = 0;
    float recoilScale_ = 1.0f, beamParticles_ = 1.0f, cacheRadius_ = 3000.0f, cacheHz_ = 4.0f, cacheTimer_ = 0, beamAccum_ = 0;
    double lastHit_ = -1.0e6, lastReport_ = -10, fireMs_ = 0;
    combat::Vec3d beamStart_, beamEnd_;
    std::vector<combat::WeaponDef> weapons_;
    std::vector<combat::HeatState> heat_;
    combat::BoltPool pool_;
    combat::Rng rng_;
    std::vector<Near> near_;
    std::vector<int> idScratch_;
    std::vector<float> lines_, lineCol_, heads_, headCol_;
};

REGISTER_MODULE(Weapons);
