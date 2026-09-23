#pragma once
// Solar flares: the sun periodically erupts and an expanding damage front sweeps outward through the system. world/solar_flares provides it.
// docs/SOLAR_FLARES.md.
//
//     auto* sf = eng.services.get<world::ISolarFlares>();       // null if the module is off (or solar_flares.enabled = false)
//     if (sf && sf->etaSeconds() >= 0) hud.text("SOLAR FLARE IN %.0fs", sf->etaSeconds());
//
// Docked ships are immune (ship::IDocking::docked()). Only the ship is damaged: never stations, planets or asteroids.

namespace world {

class ISolarFlares {
public:
    virtual ~ISolarFlares() = default;
    virtual bool active() const = 0;            // a front is expanding right now
    virtual float etaSeconds() const = 0;       // seconds to the next eruption while inside the warning window, -1 otherwise (none pending / erupting)
    virtual double shellRadius() const = 0;     // the front's radius from the sun's centre (units), -1 when not erupting
};

// ---- events (engine.events) ----
struct SolarFlareWarning { float etaSeconds = 0; };   // every fixed step inside solar_flares.warning_seconds before an eruption
struct SolarFlareErupted {};                           // once, the step the front leaves the sun
struct SolarFlareEnded {};                             // once, the step the front passes solar_flares.max_range

} // namespace world
