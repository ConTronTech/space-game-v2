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
#include "ship/ship_core/ship_api.h"
#include "ship/ship_core/ship_rules.h"

using engine::Vec3;
namespace rules = ship::rules;

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
        col_.minDamage = c.get("ship.damage_min", 1.0f, "every hit does at least this much (scratch)");

        if ((saves_ = eng.services.get<core::ISaveSystem>())) saves_->registerSaveable(this);
        if ((audio_ = eng.services.get<core::IAudio>())) hum_ = audio_->playLoop("engine_loop", 0.0f, core::Bus::Engine);

        // 1. input: nothing to bind here. Actions (thrust, strafe, lift, pitch, yaw, roll, brake)
        //    are mapped to devices in config/input/<profile>.json

        // 2. register what we draw
        render_->addPass("flight/rocks", 100, [this](core::RenderEngine&) { drawRocks(); });
        render_->addPass("flight/ship", 110, [this](core::RenderEngine&) { drawShip(); });
        eng_ = &eng;
        eng.services.provide<core::ITransformSource>(this);
        eng.services.provide<ship::IShip>(this);
        sync();

        // world content
        std::srand(1234);
        for (int i = 0; i < 2500; i++) randomDir();   // the stars moved to world/starfield; this keeps the demo rock field identical (same random sequence)
        for (int i = 0; i < 150; i++) {
            rocks_.push_back({randomDir() * (60.0f + rnd() * 800.0f), 2.0f + rnd() * 14.0f});
        }

        // collisions: the ship is a moving sphere, rocks are fixed spheres (a bit smaller than their octahedron corners)
        if ((physics_ = eng.services.get<core::IPhysics>())) {
            hullRadius_ = c.get("flight.hull_radius", 2.5f, "ship collision radius, m");
            bounce_ = c.get("flight.bounce", 0.35f, "speed kept when bouncing off a rock: 0 = dead stop, 1 = perfectly elastic");
            shipBody_ = physics_->addBody("ship", pos_, hullRadius_, true);
            for (auto& r : rocks_) rockBodies_.push_back(physics_->addBody("asteroid", r.pos, r.size * 0.8f, false));
            eng.events.subscribe<core::Collided>([this](const core::Collided& e) { onCollided(e); });
            LOG_D("ship", "%zu rocks registered with physics; first at (%.1f, %.1f, %.1f) radius %.1f", rocks_.size(),
                  rocks_[0].pos.x, rocks_[0].pos.y, rocks_[0].pos.z, rocks_[0].size * 0.8f);
        }
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        render_->removePass("flight/rocks");
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
        const float thrust = thrust_, turn = turnRate_, turnTau = turnTau_, engineTau = engineTau_;

        rules::regen(vit_, hpRegen_, dt);   // frozen while paused: fixed updates do not run then
        const bool live = vit_.alive;       // a dead ship ignores the controls and just coasts
        pitchRate_  = ease(pitchRate_,  live ? input_->value("pitch") : 0.0f,  turnTau, dt);
        yawRate_    = ease(yawRate_,    live ? input_->value("yaw") : 0.0f,    turnTau, dt);
        rollRate_   = ease(rollRate_,   live ? input_->value("roll") : 0.0f,   turnTau, dt);
        thrustOut_  = ease(thrustOut_,  live ? input_->value("thrust") : 0.0f, engineTau, dt);
        strafeOut_  = ease(strafeOut_,  live ? input_->value("strafe") : 0.0f, engineTau, dt);
        liftOut_    = ease(liftOut_,    live ? input_->value("lift") : 0.0f,   engineTau, dt);

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
        pos_ += vel_ * dt;
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
        p.pos = engine::lerp(prevPos_, pos_, a);
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
        return engine::Json::object().set("pos", vec(pos_)).set("vel", vec(vel_)).set("fwd", vec(fwd_)).set("up", vec(up_))
            .set("hp", vit_.hp).set("maxHp", vit_.maxHp).set("shield", vit_.shield)
            .set("shieldInstalled", vit_.shieldInstalled).set("shieldEnabled", vit_.shieldEnabled)
            .set("warpFuel", vit_.warpFuel).set("alive", vit_.alive);
    }
    void load(const engine::Json& j) override {
        pos_ = readVec(j["pos"], pos_);
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
    Vec3 position() const override { return pos_; }
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
        Vec3 n = shipIsA ? c.normal : c.normal * -1.0f;             // pointing from the ship to the other body
        Vec3 otherPos = shipIsA ? c.posB : c.posA;
        float otherR = shipIsA ? c.radiusB : c.radiusA;
        pos_ = otherPos - n * (hullRadius_ + otherR + 0.02f);
        // bounce off the CLOSING speed the physics reports (relative to the other body, which may be orbiting), so a planet
        // sweeping into a parked ship knocks it away instead of swallowing it; for fixed rocks this equals the old dot(vel, n)
        float into = c.speed;
        if (into > 0) vel_ -= n * ((1.0f + bounce_) * into);
        physics_->teleport(shipBody_, pos_);
        if (audio_ && c.speed > 1.0f) audio_->play("impact", std::clamp(c.speed / 40.0f, 0.25f, 1.0f));
        const std::string& kind = shipIsA ? c.kindB : c.kindA;
        float dmg = rules::collisionDamage(kind, otherR, c.speed, col_);
        LOG_D("ship", "hit %s (radius %.0f) at %.1f m/s closing: damage %.1f, now moving %.1f m/s", kind.c_str(), otherR, c.speed, dmg, engine::length(vel_));
        applyDamage(dmg, kind);
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
    float hpRegen_ = 0.2f, maxHpCap_ = 200.0f;
    Vec3 spawnPos_{0, 0, 0};
    int hum_ = 0;
    // frame-rate independent exponential easing of 'cur' toward 'target'
    static float ease(float cur, float target, float tau, float dt) {
        return cur + (target - cur) * (1.0f - std::exp(-dt / tau));
    }

    float thrust_ = 40, turnRate_ = 1.4f, turnTau_ = 0.25f, engineTau_ = 0.3f;
    float drift_ = 1, assist_ = 4, brake_ = 2, maxSpeed_ = 0;

    float pitchRate_ = 0, yawRate_ = 0, rollRate_ = 0;       // smoothed turn inputs
    float thrustOut_ = 0, strafeOut_ = 0, liftOut_ = 0;      // smoothed engine output
    Vec3 pos_{0, 0, 0}, vel_{0, 0, 0};
    Vec3 fwd_{0, 0, -1}, up_{0, 1, 0}, right_{1, 0, 0};
    Vec3 prevPos_{0, 0, 0}, prevFwd_{0, 0, -1}, prevUp_{0, 1, 0};
    std::vector<Rock> rocks_;
};

REGISTER_MODULE(ShipCore);
