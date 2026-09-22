// ship/ship_core - the player's ship: Newtonian flight, HP / shield / warp-fuel storage, collision damage, death.
// Provides ship::IShip (see ship_api.h and docs/SHIP.md). Pure rules live in ship_rules.h. The HUD is ui/ship_hud's job.
#include <GL/gl.h>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include "core/audio/audio_api.h"
#include "core/camera/camera_api.h"
#include "core/input_handler/input_api.h"
#include "core/physics_world/physics_api.h"
#include "core/render_engine/render_engine.h"
#include "core/save_system/save_api.h"
#include "engine/engine.h"
#include "engine/log.h"
#include "engine/math.h"
#include "ship/cockpit/ship_model_api.h"
#include "ship/ship_core/ship_api.h"
#include "ship/ship_core/ship_rules.h"

using engine::Vec3;
using engine::Vec3d;
namespace rules = ship::rules;

// The ship's position is a DOUBLE (world::Vec3d): see docs/PRECISION.md. Velocity, orientation and everything ship-relative stay float.
// Absolute positions are only narrowed to float where the value is cosmetic or already small (the drawn pose, the float position() accessor).

class ShipCore : public engine::Module, public ship::IShip, public core::ISaveable, public core::ITransformSource {
public:
    const char* name() const override { return "ship/ship_core"; }
    // use these if they are loaded, and initialise after them (none are required)
    std::vector<std::string> optionalDependencies() const override {
        return {"core/settings", "core/save_system", "core/audio", "core/physics_world", "core/camera"};
    }
    std::vector<std::string> dependencies() const override {
        return {"core/input_handler", "core/render_engine"};
    }

    bool init(engine::Engine& eng) override {
        input_ = &eng.services.require<core::IInput>();
        render_ = &eng.services.require<core::RenderEngine>();

        // tunables: base values live here, config/game.json can override them (see docs/CONFIG.md)
        auto& c = eng.config;
        thrust_      = c.get("flight.thrust", 40.0f, "forward acceleration at full throttle, m/s^2");
        turnRate_    = c.get("flight.turn_rate", 1.4f, "max turn rate, rad/s");
        turnTau_     = c.get("flight.turn_tau", 0.25f, "seconds to reach ~63% of a new turn rate (higher = heavier ship)");
        engineTau_   = c.get("flight.engine_tau", 0.30f, "seconds for thrust to spool up/down (higher = laggier engines)");
        drift_       = std::clamp(c.get("flight.drift", 1.0f, "1.0 = pure Newtonian, nothing slows you. 0.0 = strong flight assist"), 0.0f, 1.0f);
        assist_      = c.get("flight.assist_strength", 4.0f, "damping per second when drift is 0 (scales with 1 - drift)");
        brake_       = c.get("flight.brake", 2.0f, "speed decay per second while braking");
        maxSpeed_    = c.get("flight.max_speed", 0.0f, "speed cap in m/s, 0 = unlimited");

        // ship stats (old game values)
        auto& st = vit_;
        st.maxHp = c.get("ship.max_hp", 100.0f, "base hull points");
        st.maxShield = c.get("ship.max_shield", 200.0f, "shield capacity when a shield generator is installed");
        st.maxWarpFuel = c.get("ship.max_warp_fuel", 100.0f, "warp fuel tank size");
        st.hp = st.maxHp;
        st.warpFuel = st.maxWarpFuel;
        hpRegen_ = c.get("ship.hp_regen", 0.2f, "passive hull regeneration, HP per second (alive only)");
        maxHpCap_ = c.get("ship.max_hp_cap", 200.0f, "highest max HP hull plating can reach");
        col_.refSpeed = c.get("ship.damage_ref_speed", 200.0f, "closing speed (m/s) that counts as full impact damage");
        col_.asteroidSmall = c.get("ship.damage_asteroid_small", 10.0f, "full-speed damage of a hit on an asteroid smaller than 5 m");
        col_.asteroidMed = c.get("ship.damage_asteroid_med", 20.0f, "full-speed damage of a hit on an asteroid 5-30 m");
        col_.asteroidBig = c.get("ship.damage_asteroid_big", 35.0f, "full-speed damage of a hit on an asteroid over 30 m");
        col_.planet = c.get("ship.damage_planet", 50.0f, "full-speed damage of a hit on a planet or moon");
        col_.other = c.get("ship.damage_other", 10.0f, "full-speed damage of a hit on anything else");
        col_.sunKills = c.get("ship.sun_kills", true, "touching the sun is instant death (false = it only hurts like a planet)");
        col_.minDamage = c.get("ship.damage_min", 1.0f, "every hit that counts does at least this much (scratch)");
        col_.minSpeed = c.get("ship.damage_min_speed", 3.0f, "closing speed (m/s) below which a touch is a scrape: no damage, no sound, no bounce (you slide along the surface)");
        col_.cooldown = c.get("ship.contact_cooldown", 0.6f, "seconds before the SAME body (rock, planet, station) can damage the ship again");

        parseHold(eng.flagValue("hold"));   // dev aid: --hold=thrust:1[,strafe:0.5,...] holds those axes at that value (no keyboard needed: saved-game tests of contact damage)
        parseStart(eng.flagValue("ship-start"));                    // dev: --ship-start=X,Y,Z puts the ship (and its spawn point) there, in double
        precisionLog_ = std::atol(eng.flagValue("precision-log", "0").c_str());   // dev: --precision-log=N logs N fixed steps of position deltas (docs/PRECISION.md)
        if ((saves_ = eng.services.get<core::ISaveSystem>())) saves_->registerSaveable(this);
        if ((audio_ = eng.services.get<core::IAudio>())) hum_ = audio_->playLoop("engine_loop", 0.0f, core::Bus::Engine);

        // 1. input: nothing to bind here. Actions (thrust, strafe, lift, pitch, yaw, roll, brake)
        //    are mapped to devices in config/input/<profile>.json

        // 2. register what we draw
        demoRocks_ = c.get("flight.demo_rocks", false, "the old demo rocks near the start (150 fixed rocks, for testing); the real asteroids are world/asteroids");
        if (demoRocks_) render_->addPass("flight/rocks", 100, [this](core::RenderEngine&) { drawRocks(); });
        render_->addPass("flight/ship", 110, [this](core::RenderEngine&) { drawShip(); });
        eng_ = &eng;
        eng.services.provide<core::ITransformSource>(this);
        eng.services.provide<ship::IShip>(this);
        sync();

        // world content
        if (demoRocks_) {
            std::srand(1234);
            for (int i = 0; i < 2500; i++) randomDir();   // the stars moved to world/starfield; this keeps the demo rock field identical (same random sequence)
            for (int i = 0; i < 150; i++) {
                rocks_.push_back({randomDir() * (60.0f + rnd() * 800.0f), 2.0f + rnd() * 14.0f});
            }
        }

        // collisions: the ship is a moving sphere, rocks are fixed spheres (a bit smaller than their octahedron corners)
        if ((physics_ = eng.services.get<core::IPhysics>())) {
            hullRadius_ = c.get("flight.hull_radius", 2.5f, "ship collision radius, m");
            bounce_ = c.get("flight.bounce", 0.35f, "speed kept when bouncing off a rock: 0 = dead stop, 1 = perfectly elastic");
            shipBody_ = physics_->addBody("ship", pos_, hullRadius_, true);
            for (auto& r : rocks_) rockBodies_.push_back(physics_->addBody("asteroid", r.pos, r.size * 0.8f, false));
            eng.events.subscribe<core::Collided>([this](const core::Collided& e) { onCollided(e); });
            if (!rocks_.empty()) LOG_D("ship", "%zu rocks registered with physics; first at (%.1f, %.1f, %.1f) radius %.1f", rocks_.size(),
                  rocks_[0].pos.x, rocks_[0].pos.y, rocks_[0].pos.z, rocks_[0].size * 0.8f);
        }
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        if (demoRocks_) render_->removePass("flight/rocks");
        render_->removePass("flight/ship");
        eng.services.withdraw<core::ITransformSource>();
        eng.services.withdraw<ship::IShip>();
        if (saves_) saves_->unregisterSaveable(this);
        if (physics_) {
            physics_->removeBody(shipBody_);
            for (auto id : rockBodies_) physics_->removeBody(id);
        }
        if (audio_ && hum_) audio_->stopLoop(hum_);
    }

    void onFixedUpdate(engine::Engine&, float dt) override {
        // Controls don't snap: each axis eases toward what the player is asking for, so engines
        // spool up/down and the ship keeps turning for a moment after you let go.
        prevPos_ = pos_; prevFwd_ = fwd_; prevUp_ = up_; // for render interpolation
        cooldown_.tick(dt);
        if (held_) {   // docking holds the ship: the holder places it with setPose every step; no input, no integration
            rules::regen(vit_, hpRegen_, dt);
            pitchRate_ = yawRate_ = rollRate_ = thrustOut_ = strafeOut_ = liftOut_ = 0;
            sync();
            return;
        }
        const float thrust = thrust_, turn = turnRate_, turnTau = turnTau_, engineTau = engineTau_;

        rules::regen(vit_, hpRegen_, dt);   // frozen while paused: fixed updates do not run then
        if ((secondTimer_ += dt) >= 1.0f) {   // debug aid: how often the ship was hit, and for how much, in the last second
            secondTimer_ = 0;
            if (hitsThisSecond_ > 0) LOG_D("ship", "contacts in the last second: %d hits, %.1f damage, hp %.1f/%.0f", hitsThisSecond_, damageThisSecond_, vit_.hp, vit_.maxHp);
            hitsThisSecond_ = 0; damageThisSecond_ = 0;
        }
        const bool live = vit_.alive;       // a dead ship ignores the controls and just coasts
        pitchRate_  = ease(pitchRate_,  live ? axis("pitch") : 0.0f,  turnTau, dt);
        yawRate_    = ease(yawRate_,    live ? axis("yaw") : 0.0f,    turnTau, dt);
        rollRate_   = ease(rollRate_,   live ? axis("roll") : 0.0f,   turnTau, dt);
        thrustOut_  = ease(thrustOut_,  live ? axis("thrust") : 0.0f, engineTau, dt);
        strafeOut_  = ease(strafeOut_,  live ? axis("strafe") : 0.0f, engineTau, dt);
        liftOut_    = ease(liftOut_,    live ? axis("lift") : 0.0f,   engineTau, dt);

        // orientation: rotate the basis around its own axes
        float pitch = pitchRate_ * turn * dt;
        float yaw   = yawRate_ * turn * dt;
        float roll  = rollRate_ * turn * dt;
        fwd_ = engine::rotate(fwd_, right_, pitch);  up_ = engine::rotate(up_, right_, pitch);
        fwd_ = engine::rotate(fwd_, up_, yaw);       right_ = engine::rotate(right_, up_, yaw);
        right_ = engine::rotate(right_, fwd_, roll); up_ = engine::rotate(up_, fwd_, roll);
        fwd_ = engine::normalize(fwd_);
        right_ = engine::normalize(engine::cross(fwd_, up_));
        up_ = engine::cross(right_, fwd_);

        Vec3 acc = fwd_ * thrustOut_ + right_ * strafeOut_ + up_ * liftOut_;
        vel_ += acc * (thrust * dt);
        if (live && input_->down("brake")) vel_ *= std::exp(-brake_ * dt);
        if (drift_ < 1.0f) vel_ *= std::exp(-(1.0f - drift_) * assist_ * dt); // flight assist; drift 1.0 = none
        if (maxSpeed_ > 0.0f) {
            float sp = engine::length(vel_);
            if (sp > maxSpeed_) vel_ *= maxSpeed_ / sp;
        }
        rules::integrate(pos_, vel_, dt);                        // double position += float velocity * dt
        logPrecision(dt);
        sync();
        if (physics_) physics_->setBody(shipBody_, pos_, vel_);   // the physics world sweeps this move for hits right after us
    }

    void onUpdate(engine::Engine& eng, float) override {
        if (audio_ && hum_) {   // idle rumble, louder with throttle; silent while the menu is open
            float throttle = std::min(1.0f, std::abs(thrustOut_) + 0.6f * std::abs(strafeOut_) + 0.6f * std::abs(liftOut_));
            audio_->setLoopVolume(hum_, (eng.paused() || !vit_.alive) ? 0.0f : 0.25f + 0.75f * throttle);
        }
    }

    // The camera module calls this each frame (blending previous->current physics state by alpha).
    core::Pose transform(float a) const override {
        core::Pose p;
        p.posD = rules::lerpD(prevPos_, pos_, (double)a);         // blended in double; p.pos is the float approximation for old consumers
        p.pos = rules::toF(p.posD);
        Vec3 f = engine::normalize(engine::lerp(prevFwd_, fwd_, a));
        Vec3 u = engine::normalize(engine::lerp(prevUp_, up_, a));
        Vec3 r = engine::normalize(engine::cross(f, u));
        p.fwd = f;
        p.up = engine::cross(r, f);
        return p;
    }

    // ---- saving: pose + stats. Control easing is not saved (it settles in a fraction of a second).
    // Missing keys keep the current value, so saves from before the stats existed still load.
    const char* saveId() const override { return "gameplay/flight"; }   // kept from the flight demo so existing saves still load
    engine::Json save() const override {
        return engine::Json::object().set("pos", vecD(pos_)).set("vel", vec(vel_)).set("fwd", vec(fwd_)).set("up", vec(up_))
            .set("hp", vit_.hp).set("maxHp", vit_.maxHp).set("shield", vit_.shield)
            .set("shieldInstalled", vit_.shieldInstalled).set("shieldEnabled", vit_.shieldEnabled)
            .set("warpFuel", vit_.warpFuel).set("alive", vit_.alive);
    }
    void load(const engine::Json& j) override {
        pos_ = readVecD(j["pos"], pos_);
        vel_ = readVec(j["vel"], vel_);
        Vec3 f = engine::normalize(readVec(j["fwd"], fwd_)), u = engine::normalize(readVec(j["up"], up_));
        if (engine::length(f) < 0.5f || engine::length(u) < 0.5f || std::abs(engine::dot(f, u)) > 0.99f) { f = {0, 0, -1}; u = {0, 1, 0}; } // bad data
        fwd_ = f;
        right_ = engine::normalize(engine::cross(fwd_, u));
        up_ = engine::cross(right_, fwd_);
        pitchRate_ = yawRate_ = rollRate_ = thrustOut_ = strafeOut_ = liftOut_ = 0;
        prevPos_ = pos_; prevFwd_ = fwd_; prevUp_ = up_;   // no interpolation smear across the jump
        if (physics_ && shipBody_ != core::kNoBody) physics_->teleport(shipBody_, pos_);

        auto& v = vit_;
        v.maxHp = std::clamp((float)j["maxHp"].num(v.maxHp), 1.0f, std::max(maxHpCap_, v.maxHp));
        v.hp = std::clamp((float)j["hp"].num(v.hp), 0.0f, v.maxHp);
        v.shieldInstalled = j["shieldInstalled"].boolean(v.shieldInstalled);
        v.shieldEnabled = j["shieldEnabled"].boolean(v.shieldEnabled) && v.shieldInstalled;
        v.shield = v.shieldEnabled ? std::clamp((float)j["shield"].num(v.shield), 0.0f, v.maxShield) : 0.0f;
        v.warpFuel = std::clamp((float)j["warpFuel"].num(v.warpFuel), 0.0f, v.maxWarpFuel);
        v.alive = j["alive"].boolean(v.hp > 0.0f) && v.hp > 0.0f;
        sync();
    }

    // ---- ship::IShip ----
    const ship::ShipStatus& status() const override { return status_; }
    Vec3 position() const override { return rules::toF(pos_); }   // approximation: see positionD()
    Vec3d positionD() const override { return pos_; }
    Vec3 velocity() const override { return vel_; }
    Vec3 forward() const override { return fwd_; }

    void applyDamage(float amount, const std::string& source) override {
        auto r = rules::applyDamage(vit_, amount);
        if (r.toHull <= 0.0f && r.absorbed <= 0.0f) return;   // dead, or nothing to apply
        sync();
        eng_->events.emit(ship::DamageTaken{r.toHull, r.absorbed, source});
        if (r.shieldBroken) eng_->events.emit(ship::ShieldBroken{});
        if (r.died) die(source);
    }
    void heal(float hp) override { rules::heal(vit_, hp); sync(); }
    void addWarpFuel(float amount) override { rules::addWarpFuel(vit_, amount); sync(); }
    bool consumeWarpFuel(float amount) override {
        bool emptied = false;
        bool ok = rules::consumeWarpFuel(vit_, amount, emptied);
        sync();
        if (emptied) eng_->events.emit(ship::FuelEmpty{});
        return ok;
    }
    void addMaxHp(float amount) override { rules::addMaxHp(vit_, amount, maxHpCap_); sync(); }
    void installShield(bool enabled) override { rules::installShield(vit_, enabled); sync(); }
    void setHeld(bool h) override {
        if (h && !held_) { prevPos_ = pos_; prevFwd_ = fwd_; prevUp_ = up_; pitchRate_ = yawRate_ = rollRate_ = thrustOut_ = strafeOut_ = liftOut_ = 0; }
        held_ = h;
    }
    void setPose(const Vec3& p, const Vec3& f, const Vec3& u) override { setPoseD({p.x, p.y, p.z}, f, u); }
    void setPoseD(const Vec3d& p, const Vec3& f, const Vec3& u) override {
        Vec3 nf = engine::normalize(f);
        if (engine::length(nf) < 0.5f) return;                          // bad direction: keep the current orientation
        Vec3 r = engine::normalize(engine::cross(nf, u));
        if (engine::length(r) < 0.5f) return;                           // up parallel to forward
        pos_ = p; fwd_ = nf; right_ = r; up_ = engine::cross(r, nf);
        pitchRate_ = yawRate_ = rollRate_ = 0;
        sync();
        // prev* is left alone: onFixedUpdate copied the previous pose into it at the start of this step, so the camera blends smoothly from there
        if (physics_ && shipBody_ != core::kNoBody) physics_->setBody(shipBody_, pos_, vel_);
    }
    void setWarping(bool w) override { status_.warping = w; }   // sync() leaves it alone
    void setVelocity(const Vec3& v) override {
        vel_ = v;
        sync();
        if (physics_ && shipBody_ != core::kNoBody) physics_->setBody(shipBody_, pos_, vel_);
    }
    void kill(const std::string& cause) override {
        if (!rules::kill(vit_)) return;
        sync();
        die(cause);
    }
    void respawn() override {
        vel_ = {0, 0, 0};
        pos_ = spawnPos_;
        fwd_ = {0, 0, -1}; up_ = {0, 1, 0}; right_ = {1, 0, 0};
        pitchRate_ = yawRate_ = rollRate_ = thrustOut_ = strafeOut_ = liftOut_ = 0;
        prevPos_ = pos_; prevFwd_ = fwd_; prevUp_ = up_;
        vit_.hp = vit_.maxHp;                  // like the old game: full hull + full fuel; the shield is not restored
        vit_.warpFuel = vit_.maxWarpFuel;
        vit_.alive = true;
        sync();
        if (physics_ && shipBody_ != core::kNoBody) physics_->teleport(shipBody_, pos_);
        eng_->events.emit(ship::Respawned{});
    }

private:
    // Bounce off whatever we hit: put the hull just outside it and reflect the velocity component going into it.
    void onCollided(const core::Collided& c) {
        bool shipIsA = c.a == shipBody_;
        if (!shipIsA && c.b != shipBody_) return;
        if (held_ && (shipIsA ? c.kindB : c.kindA) == "station") return;   // docking: the ship rides ON the station, touching it is the point
        Vec3 n = shipIsA ? c.normal : c.normal * -1.0f;             // pointing from the ship to the other body
        Vec3d otherPos = shipIsA ? c.posBd : c.posAd;               // double contact position: the push-out is a metre-scale offset from it
        float otherR = shipIsA ? c.radiusB : c.radiusA;
        pos_ = rules::offsetD(otherPos, n * -(hullRadius_ + otherR + 0.02f));
        // The CLOSING speed the physics reports is relative to the other body (which may be orbiting), so a planet sweeping into a parked ship knocks it
        // away instead of swallowing it; for fixed rocks this equals the old dot(vel, n).
        // Real impact (>= ship.damage_min_speed): bounce as before. Resting / scraping contact (holding thrust into a surface): the closing component is
        // just removed after the pushout, so the ship slides instead of re-entering the surface every step (which used to hit again and again).
        const std::string& kind = shipIsA ? c.kindB : c.kindA;
        const bool scrape = !(c.speed >= col_.minSpeed) && !(kind == "sun" && col_.sunKills);
        vel_ = rules::velocityAfterContact(vel_, n, c.speed, bounce_, col_.minSpeed);
        physics_->teleport(shipBody_, pos_);
        if (audio_ && !scrape) audio_->play("impact", std::clamp(c.speed / 40.0f, 0.25f, 1.0f));
        const int otherId = shipIsA ? c.b : c.a;
        float dmg = rules::contactDamage(kind, otherR, c.speed, col_);
        const bool lethal = kind == "sun" && col_.sunKills;
        if (dmg > 0.0f && !lethal && !cooldown_.ready(otherId)) dmg = 0.0f;         // the same body hurt us a moment ago: bounce, but no second hit
        LOG_D("ship", "hit %s (radius %.0f) at %.1f m/s closing: %s, damage %.1f, now moving %.1f m/s", kind.c_str(), otherR, c.speed, scrape ? "scrape" : "impact", dmg, engine::length(vel_));
        if (dmg > 0.0f) {
            cooldown_.arm(otherId, col_.cooldown);
            hitsThisSecond_++; damageThisSecond_ += dmg;
            applyDamage(dmg, kind);
        }
    }

    // --ship-start=X,Y,Z (dev): start somewhere far from the origin to test precision there. The numbers are read as doubles.
    void parseStart(const std::string& spec) {
        if (spec.empty()) return;
        double v[3] = {0, 0, 0};
        size_t pos = 0;
        for (int i = 0; i < 3 && pos <= spec.size(); i++) {
            size_t comma = spec.find(',', pos);
            v[i] = std::strtod(spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos).c_str(), nullptr);
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        pos_ = prevPos_ = spawnPos_ = Vec3d{v[0], v[1], v[2]};
        LOG_I("ship", "--ship-start: %.3f, %.3f, %.3f", pos_.x, pos_.y, pos_.z);
    }

    // --precision-log=N (dev): the per-step position delta against velocity * dt, for the REAL game loop. Smooth flight means the two match
    // every step. Alongside the live double position it carries a float MIRROR - a second position integrated exactly the way ship_core did
    // before this change (`posF += vel * dt`, float) from the same starting point with the same velocity - so one run prints the before and the
    // after side by side: far from the origin the mirror shows 0 on most steps and a whole float ULP (1 m at 1e7, 512 m at 5e9) on the rest.
    void logPrecision(float dt) {
        if (precisionLog_ <= 0) return;
        precisionLog_--;
        if (!mirrorStarted_) { mirrorStarted_ = true; mirror_ = rules::toF(prevPos_); mirrorFrom_ = prevPos_; }
        Vec3 mirrorPrev = mirror_;
        mirror_ += vel_ * dt;                                        // the OLD integration, bit for bit
        Vec3d d{pos_.x - prevPos_.x, pos_.y - prevPos_.y, pos_.z - prevPos_.z};
        double moved = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), want = (double)engine::length(vel_) * dt;
        double movedF = (double)engine::length(mirror_ - mirrorPrev);
        precisionWorst_ = std::max(precisionWorst_, std::abs(moved - want));
        precisionWorstF_ = std::max(precisionWorstF_, std::abs(movedF - want));
        if (movedF == 0.0 && want > 0.0) mirrorStalls_++;             // a frame the old float position did not move at all
        // how far each path has travelled since the log started: the double one is the truth, the mirror is what the player used to get
        Vec3d travD{pos_.x - mirrorFrom_.x, pos_.y - mirrorFrom_.y, pos_.z - mirrorFrom_.z};
        Vec3d travF{(double)mirror_.x - mirrorFrom_.x, (double)mirror_.y - mirrorFrom_.y, (double)mirror_.z - mirrorFrom_.z};
        LOG_I("ship", "precision: |pos| %.1f, step double %.6f / float %.6f, velocity*dt %.6f, step error double %.3e / float %.3e "
                      "(worst %.3e / %.3e), travelled double %.4f / float %.4f, float stalled %ld frames, pos (%.4f, %.4f, %.4f)",
              std::sqrt(pos_.x * pos_.x + pos_.y * pos_.y + pos_.z * pos_.z), moved, movedF, want, moved - want, movedF - want,
              precisionWorst_, precisionWorstF_, std::sqrt(travD.x * travD.x + travD.y * travD.y + travD.z * travD.z),
              std::sqrt(travF.x * travF.x + travF.y * travF.y + travF.z * travF.z), mirrorStalls_, pos_.x, pos_.y, pos_.z);
    }

    // --hold=name:value,...: an axis held at a value instead of the input's (thrust, strafe, lift, pitch, yaw, roll)
    void parseHold(const std::string& spec) {
        size_t pos = 0;
        while (pos < spec.size()) {
            size_t comma = spec.find(',', pos);
            std::string item = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            size_t colon = item.find(':');
            if (colon != std::string::npos) {
                std::string name = item.substr(0, colon);
                float v = (float)std::atof(item.c_str() + colon + 1);
                if (name == "thrust") holdThrust_ = v; else if (name == "strafe") holdStrafe_ = v; else if (name == "lift") holdLift_ = v;
                else if (name == "pitch") holdPitch_ = v; else if (name == "yaw") holdYaw_ = v; else if (name == "roll") holdRoll_ = v;
                else LOG_W("ship", "--hold: unknown axis '%s'", name.c_str());
            }
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    }
    float axis(const char* name) const {
        const std::string n = name;
        const float held = n == "thrust" ? holdThrust_ : n == "strafe" ? holdStrafe_ : n == "lift" ? holdLift_ : n == "pitch" ? holdPitch_ : n == "yaw" ? holdYaw_ : holdRoll_;
        return held != kNoHold ? held : input_->value(n);
    }

    // mirror the internal vitals + speed into the public status
    void sync() {
        auto& s = status_;
        s.hp = vit_.hp; s.maxHp = vit_.maxHp;
        s.shield = vit_.shield; s.maxShield = vit_.maxShield;
        s.shieldInstalled = vit_.shieldInstalled; s.shieldEnabled = vit_.shieldEnabled;
        s.warpFuel = vit_.warpFuel; s.maxWarpFuel = vit_.maxWarpFuel;
        s.alive = vit_.alive;
        s.speed = engine::length(vel_);
    }
    void die(const std::string& cause) { LOG_I("ship", "destroyed by %s", cause.c_str()); eng_->events.emit(ship::Died{cause}); }

    static engine::Json vec(const Vec3& v) { return engine::Json::array().push(v.x).push(v.y).push(v.z); }
    static Vec3 readVec(const engine::Json& j, Vec3 def) {
        if (j.size() < 3) return def;
        return {(float)j.at(0).num(def.x), (float)j.at(1).num(def.y), (float)j.at(2).num(def.z)};
    }
    // The position round-trips as full double: Json numbers are doubles and are printed with the shortest text that reads back exactly.
    static engine::Json vecD(const Vec3d& v) { return engine::Json::array().push(v.x).push(v.y).push(v.z); }
    static Vec3d readVecD(const engine::Json& j, Vec3d def) {
        if (j.size() < 3) return def;
        return {j.at(0).num(def.x), j.at(1).num(def.y), j.at(2).num(def.z)};
    }

    struct Rock { Vec3 pos; float size; };

    static float rnd() { return std::rand() / (float)RAND_MAX; }
    static Vec3 randomDir() {
        Vec3 v;
        do { v = {rnd() * 2 - 1, rnd() * 2 - 1, rnd() * 2 - 1}; } while (engine::length(v) > 1.0f || engine::length(v) < 0.1f);
        return engine::normalize(v);
    }

    // Wireframe fighter, drawn only when the camera is outside the ship (chase view).
    void drawShip() {
        auto* cam = eng_->services.get<core::ICamera>();
        if (!cam || !cam->showsShip()) return;
        if (auto* model = eng_->services.get<cockpit::IShipModel>()) if (model->drawnInChase()) return;   // the real ShipV2 is drawn instead (ship/cockpit)
        core::Pose p = transform(eng_->alpha());
        Vec3 r = engine::normalize(engine::cross(p.fwd, p.up));
        float m[16] = {r.x, r.y, r.z, 0,   p.up.x, p.up.y, p.up.z, 0,   -p.fwd.x, -p.fwd.y, -p.fwd.z, 0,   p.pos.x, p.pos.y, p.pos.z, 1};
        glMultMatrixf(m);                                    // local space: +x right, +y up, -z forward
        const float v[5][3] = {{0, 0, -3.0f}, {-1.6f, 0, 1.5f}, {1.6f, 0, 1.5f}, {0, 0.8f, 1.2f}, {0, -0.4f, 1.2f}};
        const int e[9][2] = {{0, 1}, {0, 2}, {0, 3}, {0, 4}, {1, 2}, {1, 3}, {2, 3}, {1, 4}, {2, 4}};
        glLineWidth(1.5f);
        glColor3f(0.4f, 0.9f, 1.0f);
        glBegin(GL_LINES);
        for (auto& ed : e) { glVertex3fv(v[ed[0]]); glVertex3fv(v[ed[1]]); }
        glEnd();
        glLineWidth(1.0f);
    }

    void drawRocks() {
        glColor3f(0.55f, 0.5f, 0.45f);
        for (auto& r : rocks_) {
            glPushMatrix();
            glTranslatef(r.pos.x, r.pos.y, r.pos.z);
            float s = r.size;
            const float v[6][3] = {{s,0,0},{-s,0,0},{0,s,0},{0,-s,0},{0,0,s},{0,0,-s}};
            const int e[12][2] = {{0,2},{0,3},{0,4},{0,5},{1,2},{1,3},{1,4},{1,5},{2,4},{4,3},{3,5},{5,2}};
            glBegin(GL_LINES);
            for (auto& ed : e) { glVertex3fv(v[ed[0]]); glVertex3fv(v[ed[1]]); }
            glEnd();
            glPopMatrix();
        }
    }

    core::IInput* input_ = nullptr;
    engine::Engine* eng_ = nullptr;
    core::RenderEngine* render_ = nullptr;
    core::ISaveSystem* saves_ = nullptr;
    core::IAudio* audio_ = nullptr;
    core::IPhysics* physics_ = nullptr;
    core::BodyId shipBody_ = core::kNoBody;
    std::vector<core::BodyId> rockBodies_;
    float hullRadius_ = 2.5f, bounce_ = 0.35f;
    ship::rules::Vitals vit_;
    ship::ShipStatus status_;
    ship::rules::CollisionParams col_;
    ship::rules::ContactCooldown cooldown_;
    float hpRegen_ = 0.2f, maxHpCap_ = 200.0f;
    Vec3d spawnPos_{0, 0, 0};
    int hum_ = 0;
    // frame-rate independent exponential easing of 'cur' toward 'target'
    static float ease(float cur, float target, float tau, float dt) {
        return cur + (target - cur) * (1.0f - std::exp(-dt / tau));
    }

    float thrust_ = 40, turnRate_ = 1.4f, turnTau_ = 0.25f, engineTau_ = 0.3f;
    float drift_ = 1, assist_ = 4, brake_ = 2, maxSpeed_ = 0;

    float pitchRate_ = 0, yawRate_ = 0, rollRate_ = 0;       // smoothed turn inputs
    float thrustOut_ = 0, strafeOut_ = 0, liftOut_ = 0;      // smoothed engine output
    Vec3d pos_{0, 0, 0};
    Vec3 vel_{0, 0, 0};
    Vec3 fwd_{0, 0, -1}, up_{0, 1, 0}, right_{1, 0, 0};
    Vec3d prevPos_{0, 0, 0};
    Vec3 prevFwd_{0, 0, -1}, prevUp_{0, 1, 0};
    std::vector<Rock> rocks_;
    bool demoRocks_ = false;
    static constexpr float kNoHold = -99.0f;
    float holdThrust_ = kNoHold, holdStrafe_ = kNoHold, holdLift_ = kNoHold, holdPitch_ = kNoHold, holdYaw_ = kNoHold, holdRoll_ = kNoHold;
    long precisionLog_ = 0;
    double precisionWorst_ = 0, precisionWorstF_ = 0;
    Vec3 mirror_{0, 0, 0};               // --precision-log: the float position the OLD code would have had, integrated beside the real one
    Vec3d mirrorFrom_{0, 0, 0};          // where both started, so the two travelled distances can be compared
    long mirrorStalls_ = 0;
    bool mirrorStarted_ = false;
    int hitsThisSecond_ = 0;
    float damageThisSecond_ = 0, secondTimer_ = 0;
    bool held_ = false;                                       // setHeld(): placed by the docking module
};

REGISTER_MODULE(ShipCore);
