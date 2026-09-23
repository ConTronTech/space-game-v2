// world/solar_flares - the sun periodically erupts: a warning countdown, then an expanding damage front sweeps out through the system and hits the
// ship once as it passes (worse closer to the sun; docked ships are immune). Provides world::ISolarFlares. Rules in flare_rules.h; design in
// docs/SOLAR_FLARES.md. No save: the next flare is simply re-rolled from the seed on load.
#include <cmath>
#include "engine/engine.h"
#include "engine/log.h"
#include "ship/docking/docking_api.h"
#include "ship/ship_core/ship_api.h"
#include "ui/toast/toast_api.h"
#include "world/solar_flares/flare_rules.h"
#include "world/solar_flares/solar_flares_api.h"
#include "world/star_system/star_system_api.h"

class SolarFlares : public engine::Module, public world::ISolarFlares {
public:
    const char* name() const override { return "world/solar_flares"; }
    std::vector<std::string> dependencies() const override { return {"world/star_system"}; }
    std::vector<std::string> optionalDependencies() const override { return {"ship/ship_core", "ship/docking", "ui/toast"}; }

    bool init(engine::Engine& eng) override {
        auto& c = eng.config;
        if (!c.get("solar_flares.enabled", true, "the sun periodically erupts; the front damages the ship as it passes (docs/SOLAR_FLARES.md)")) return true;
        sys_ = eng.services.get<world::IStarSystem>();
        if (!sys_) { LOG_W("solar_flares", "no star system: no solar flares"); return true; }

        const auto& bodies = sys_->bodies();
        double sunR = bodies.empty() ? 1000.0 : bodies[0].radius, outer = 0;
        for (const world::Body& b : bodies) if (b.kind == world::BodyKind::Planet) outer = std::max(outer, b.orbitRadius);

        world::FlareParams p;
        p.intervalMin = c.get("solar_flares.interval_min", 90.0f, "seconds: shortest gap between one flare ending and the next eruption");
        p.intervalMax = c.get("solar_flares.interval_max", 240.0f, "seconds: longest gap (each gap is a seeded roll in [min, max])");
        p.warningSeconds = c.get("solar_flares.warning_seconds", 15.0f, "seconds of countdown warning before an eruption");
        p.shellSpeed = c.get("solar_flares.shell_speed", 4000.0f, "units/s the flare front expands outward from the sun");
        p.shellThickness = c.get("solar_flares.shell_thickness", 1500.0f, "units: width of the flare front (the ship is hit while inside it)");
        const float maxRange = c.get("solar_flares.max_range", 0.0f, "units from the sun: the flare ends past this (0 = auto: 3x the outermost planet orbit)");
        p.maxRange = maxRange > 0 ? (double)maxRange : world::autoFlareMaxRange(outer, sunR);
        p.damageAtSun = c.get("solar_flares.damage_at_sun", 120.0f, "damage (shield first) when hit near the sun, falling linearly to 0 at damage_falloff_range");
        p.damageMinRange = c.get("solar_flares.damage_min_range", 5000.0f, "units from the sun's centre: always full damage inside this");
        p.damageFalloffRange = c.get("solar_flares.damage_falloff_range", 30000.0f,
            "units from the sun's centre: damage reaches 0 here (independent of max_range, which is just where the front stops existing - "
            "max_range is far past the outer planets, so damage would barely drop there without its own, much shorter falloff)");
        p.startRadius = sunR;
        params_ = world::sanitizeFlareParams(p);

        const unsigned seed = (unsigned)c.get("world.seed", 1234.0f, "seed of the star system (same seed = same system)");
        state_ = world::startFlareCycle(seed ^ 0x5F1A7Eu, params_);
        if (eng.hasFlag("solar-flare-now")) {   // test flag: the first flare comes right after its warning
            state_.untilEruption = params_.warningSeconds + 1.0;
            LOG_I("solar_flares", "--solar-flare-now: first eruption in %.0fs", state_.untilEruption);
        }
        eng.services.provide<world::ISolarFlares>(this);
        on_ = true;
        LOG_I("solar_flares", "first flare in %.0fs; interval %.0f-%.0fs, warning %.0fs, front %.0f u/s x %.0f wide to %.0f, damage %.0f (full inside %.0f, 0 past %.0f)",
              state_.untilEruption, params_.intervalMin, params_.intervalMax, params_.warningSeconds, params_.shellSpeed, params_.shellThickness,
              params_.maxRange, params_.damageAtSun, params_.damageMinRange, params_.damageFalloffRange);
        return true;
    }

    void shutdown(engine::Engine& eng) override {
        if (on_) eng.services.withdraw<world::ISolarFlares>();
        on_ = false;
    }

    // Fixed step: frozen while paused, like the rest of the simulation.
    void onFixedUpdate(engine::Engine& eng, float dt) override {
        if (!on_) return;
        const world::FlareStep st = world::stepFlare(state_, params_, dt);
        eta_ = (float)st.warningEta;
        if (st.warningEta >= 0) {
            eng.events.emit(world::SolarFlareWarning{eta_});
            if (!warned_) {
                warned_ = true;
                LOG_I("solar_flares", "SolarFlareWarning: eruption in %.0fs", st.warningEta);
                if (auto* t = eng.services.get<core::IToast>())
                    t->show("SOLAR FLARE IN " + std::to_string((int)std::ceil(st.warningEta)) + "s - dock at a station", core::IToast::Level::Warning, 6.0f);
            }
        }
        if (st.erupted) {
            warned_ = false;
            hit_ = false;
            LOG_I("solar_flares", "SolarFlareErupted (flare %u)", state_.cycle);
            eng.events.emit(world::SolarFlareErupted{});
            if (auto* t = eng.services.get<core::IToast>()) t->show("SOLAR FLARE ERUPTED", core::IToast::Level::Urgent, 5.0f);
        }
        if (st.radius >= 0 && !hit_) hitShip(eng, st);
        if (st.ended) {
            LOG_I("solar_flares", "SolarFlareEnded; next eruption in %.0fs", state_.untilEruption);
            eng.events.emit(world::SolarFlareEnded{});
        }
    }

    // ---- world::ISolarFlares ----
    bool active() const override { return on_ && state_.erupting; }
    float etaSeconds() const override { return on_ ? eta_ : -1.0f; }
    double shellRadius() const override { return on_ && state_.erupting ? state_.radius : -1.0; }

private:
    // One hit per flare: the swept front test, the distance falloff, skipped entirely while docked.
    void hitShip(engine::Engine& eng, const world::FlareStep& st) {
        ship::IShip* ship = eng.services.get<ship::IShip>();
        if (!ship || !ship->status().alive) return;
        const engine::Vec3d sp = ship->positionD();
        const world::Vec3d sun = sys_->sunPosition();
        const double dx = sp.x - sun.x, dy = sp.y - sun.y, dz = sp.z - sun.z, d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!world::flareShellHits(d, st.prevRadius, st.radius, params_.shellThickness)) return;
        hit_ = true;   // the front passed the ship this flare, docked or not: it does not come back for a second try after undocking
        if (const ship::IDocking* dock = eng.services.get<ship::IDocking>(); dock && dock->docked()) {
            LOG_I("solar_flares", "front passed the ship at %.0f units: docked, no damage", d);
            return;
        }
        const float dmg = world::flareDamage(d, params_);
        LOG_I("solar_flares", "front hit the ship at %.0f units from the sun: %.1f damage", d, dmg);
        if (dmg > 0) ship->applyDamage(dmg, "solar_flare");
    }

    world::IStarSystem* sys_ = nullptr;
    world::FlareParams params_;
    world::FlareState state_;
    float eta_ = -1;
    bool on_ = false, warned_ = false, hit_ = false;
};

REGISTER_MODULE(SolarFlares);
