#pragma once
// Pure respawn state machine (no SDL/GL/engine types): idle -> counting on death -> one "respawn now" trigger -> idle until alive again.
// Unit-tested in tests/test_respawn.cpp.
#include <algorithm>

namespace ship {

constexpr float kMinRespawnSeconds = 0.5f;

class RespawnRules {
public:
    void configure(float seconds, bool enabled) { seconds_ = std::max(kMinRespawnSeconds, seconds); enabled_ = enabled; }

    // Call once per frame. Returns true exactly once per death: the frame the countdown runs out (the caller then respawns the ship).
    // 'paused' freezes the countdown. A ship that is alive again (respawned, loaded save) cancels and re-arms everything.
    bool update(bool alive, float dt, bool paused) {
        if (alive) { counting_ = false; fired_ = false; left_ = 0; return false; }
        if (!enabled_ || fired_) { counting_ = false; left_ = 0; return false; }
        if (!counting_) { counting_ = true; left_ = seconds_; return false; }
        if (paused) return false;
        left_ -= dt;
        if (left_ > 0) return false;
        counting_ = false; left_ = 0; fired_ = true;
        return true;
    }

    bool counting() const { return counting_; }
    float secondsLeft() const { return counting_ ? left_ : 0.0f; }
    float seconds() const { return seconds_; }

private:
    float seconds_ = 3.0f, left_ = 0;
    bool enabled_ = true, counting_ = false, fired_ = false;
};

} // namespace ship
