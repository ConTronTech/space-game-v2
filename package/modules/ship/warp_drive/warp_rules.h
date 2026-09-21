#pragma once
// Pure warp-drive rules: no SDL, no GL, no engine types. Unit-tested in package/tests/test_warp.cpp.
// warp_drive.cpp owns the input, the IShip calls and the logging; the maths lives here.
#include <algorithm>
#include <cmath>

namespace warp {

struct V3 { float x = 0, y = 0, z = 0; };

struct Params {
    float speed = 2000.0f;        // warp speed cap, m/s
    float accelFactor = 2.0f;     // acceleration = speed * accelFactor (m/s^2)
    float fuelDrain = 3.33f;      // fuel per second while engaged
    float minFuel = 1.0f;         // fuel needed to engage
    float exitSpeed = 200.0f;     // speed cap applied on disengage, m/s; 0 = keep the momentum
};

inline float length(const V3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

// The toggle key: engage only when alive, not already engaged and with enough fuel.
inline bool canEngage(bool engaged, bool alive, float fuel, const Params& p) {
    return alive && !engaged && fuel >= p.minFuel;
}

struct StepResult {
    V3 velocity;                  // new velocity (unchanged when nothing happened)
    float fuelToBurn = 0;         // take this much fuel (never more than is left)
    bool disengage = false;       // the drive must shut down now
};

// One fixed step while engaged. Dead ship: disengage, nothing else. Fuel that cannot cover the step (fuel <= burn):
// burn what is left, still accelerate this last step, then disengage. dt <= 0 changes nothing.
inline StepResult step(bool alive, float fuel, const V3& vel, const V3& fwd, float dt, const Params& p) {
    StepResult r;
    r.velocity = vel;
    if (!alive) { r.disengage = true; return r; }
    if (dt <= 0.0f) return r;
    float burn = p.fuelDrain * dt;
    if (fuel <= burn) { r.fuelToBurn = std::max(0.0f, fuel); r.disengage = true; }
    else r.fuelToBurn = burn;
    float a = p.speed * p.accelFactor * dt;
    V3 v{vel.x + fwd.x * a, vel.y + fwd.y * a, vel.z + fwd.z * a};
    float sp = length(v);
    if (sp > p.speed && sp > 0.0f) { float k = p.speed / sp; v = {v.x * k, v.y * k, v.z * k}; }
    r.velocity = v;
    return r;
}

// Velocity after leaving warp: capped at exitSpeed keeping the direction; exitSpeed <= 0 keeps the momentum.
inline V3 exitVelocity(const V3& vel, const Params& p) {
    float sp = length(vel);
    if (p.exitSpeed <= 0.0f || sp <= p.exitSpeed) return vel;
    float k = p.exitSpeed / sp;
    return {vel.x * k, vel.y * k, vel.z * k};
}

} // namespace warp
