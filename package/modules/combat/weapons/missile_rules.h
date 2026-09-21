#pragma once
// Pure missile logic: no GL, no SDL, no engine types. Unit-tested in package/tests/test_missiles.cpp.
//   * ammo: the missile rack (count, cap, packs that do not fit are refused)
//   * lock-on: candidate ranking (cone, then distance) and the state machine idle -> acquiring -> locked -> lost
//   * guidance: proportional navigation turned into a heading, limited by the turn rate and the thrust
//   * one missile step (thrust while fuel lasts, coasting after), arming distance, blast damage falloff
//   * a fixed-capacity missile pool (struct of arrays, no allocation after init)
// A missile is a real object: finite fuel, finite thrust along its nose, finite turn rate. No homing that cheats physics.
#include <algorithm>
#include <cmath>
#include <vector>
#include "combat/weapons/weapons_rules.h"

namespace combat {

constexpr double kPi = 3.14159265358979323846;
inline Vec3d cross(const Vec3d& a, const Vec3d& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline Vec3d normalized(const Vec3d& a, const Vec3d& fallback = {0, 0, -1}) {
    double l = length(a);
    return l > 1e-12 ? mul(a, 1.0 / l) : fallback;
}
// Angle between two directions (any length), degrees.
inline double angleDeg(const Vec3d& a, const Vec3d& b) {
    double la = length(a), lb = length(b);
    if (la < 1e-12 || lb < 1e-12) return 0.0;
    return std::acos(std::clamp(dot(a, b) / (la * lb), -1.0, 1.0)) * 180.0 / kPi;
}

// ---- ammo ----
// How many of `n` missiles fit into a rack holding `have` of `max` (0 = full: the pack is refused and not consumed).
inline int acceptMissiles(int have, int max, int n) { return std::clamp(std::min(n, max - have), 0, std::max(0, n)); }
// A shot needs a missile; returns false (and changes nothing) when the rack is empty.
inline bool takeMissile(int& have) { if (have <= 0) return false; have--; return true; }

// ---- lock-on ----
enum class LockState { Idle = 0, Acquiring = 1, Locked = 2, Lost = 3 };
inline const char* lockStateName(LockState s) {
    switch (s) { case LockState::Acquiring: return "acquiring"; case LockState::Locked: return "locked"; case LockState::Lost: return "lost"; default: return "idle"; }
}

struct LockParams {
    double range = 2500.0;          // candidates and the kept lock must be this close (centre distance)
    double coneDeg = 12.0;          // T picks targets inside this cone around the ship's nose; acquiring must stay inside it
    double keepConeDeg = 30.0;      // a finished lock is dropped only when the target leaves this wider cone
    float lockTime = 1.0f;          // seconds inside the cone to go from acquiring to locked
    float lostShow = 1.0f;          // seconds the "lost" state is shown before it goes back to idle
    double autoConeDeg = 80.0;      // an auto-picked target is acquired and kept inside this (wide) cone: missiles turn
};

struct LockCandidate { int id = -1; Vec3d pos; double radius = 0; };

struct Lock {
    LockState state = LockState::Idle;
    int target = -1;                // asteroid id
    float timer = 0;                // acquiring: time in the cone; lost: time since lost
    const char* why = "";           // the last loss reason ("target destroyed", "out of range", "left the cone")
    bool autoPicked = false;        // chosen by auto-lock: acquire / keep inside LockParams::autoConeDeg
};

// Candidates inside the cone and range, best first (smallest angle, then nearest). `out` holds indices into `c`. No allocation if `out` has room.
inline void rankCandidates(const Vec3d& shipPos, const Vec3d& fwd, const std::vector<LockCandidate>& c, const LockParams& p, std::vector<int>& out) {
    out.clear();
    for (int i = 0; i < (int)c.size(); i++) {
        Vec3d r = sub(c[i].pos, shipPos);
        if (length(r) > p.range || angleDeg(fwd, r) > p.coneDeg) continue;
        out.push_back(i);
    }
    std::sort(out.begin(), out.end(), [&](int a, int b) {
        Vec3d ra = sub(c[a].pos, shipPos), rb = sub(c[b].pos, shipPos);
        double aa = angleDeg(fwd, ra), ab = angleDeg(fwd, rb);
        if (std::fabs(aa - ab) > 0.5) return aa < ab;                 // half a degree counts as "the same angle": then the nearer one
        return length(ra) < length(rb);
    });
}

// The T key. Locks the best candidate; with a target already chosen it cycles to the next one; when the only candidate is the current
// target, the second press clears the lock. Returns the new target id (-1 = cleared / nothing in the cone).
inline int pressLock(Lock& l, const std::vector<LockCandidate>& c, const std::vector<int>& ranked) {
    bool haveTarget = (l.state == LockState::Acquiring || l.state == LockState::Locked) && l.target >= 0;
    if (ranked.empty()) {
        if (haveTarget) return l.target;                              // nothing new in the cone: keep what we have
        return -1;
    }
    int pick = c[ranked[0]].id;
    if (haveTarget) {
        int at = -1;
        for (int k = 0; k < (int)ranked.size(); k++) if (c[ranked[k]].id == l.target) at = k;
        if (at >= 0) pick = c[ranked[(at + 1) % ranked.size()]].id;
        if (pick == l.target) { l = Lock{}; return -1; }             // second press on the same (only) target: clear
    }
    l.state = LockState::Acquiring; l.target = pick; l.timer = 0; l.why = ""; l.autoPicked = false;
    return pick;
}

inline void clearLock(Lock& l) { l = Lock{}; }

// One step of the state machine. alive / targetPos describe the current target (ignored when there is none).
inline void updateLock(Lock& l, const LockParams& p, float dt, const Vec3d& shipPos, const Vec3d& fwd, bool alive, const Vec3d& targetPos) {
    auto lose = [&](const char* why) { l.state = LockState::Lost; l.timer = 0; l.why = why; };
    switch (l.state) {
    case LockState::Idle: return;
    case LockState::Lost:
        l.timer += dt;
        if (l.timer >= p.lostShow) { l.state = LockState::Idle; l.target = -1; l.timer = 0; }
        return;
    case LockState::Acquiring:
    case LockState::Locked: {
        if (!alive) return lose("target destroyed");
        Vec3d r = sub(targetPos, shipPos);
        if (length(r) > p.range) return lose("out of range");
        double a = angleDeg(fwd, r);
        if (l.autoPicked) {
            if (a > std::max(p.autoConeDeg, p.keepConeDeg)) return lose("left the cone");
            if (l.state == LockState::Acquiring) { l.timer += dt; if (l.timer >= p.lockTime) { l.state = LockState::Locked; l.timer = 0; } }
            return;
        }
        if (l.state == LockState::Acquiring) {
            if (a > p.coneDeg) return lose("left the cone");
            l.timer += dt;
            if (l.timer >= p.lockTime) { l.state = LockState::Locked; l.timer = 0; }
        } else if (a > p.keepConeDeg) return lose("left the cone");
        return;
    }
    }
}

// ---- auto-lock ----
// While the missile weapon is selected the lock picks the NEAREST alive rock ahead by itself (wheels and pads have no spare buttons).
// Hysteresis: a scan runs only without a lock (idle / lost); an acquiring or locked target is kept until updateLock loses it.
struct AutoLockParams {
    bool enabled = true;            // combat.auto_lock
    float hz = 4.0f;                // combat.auto_lock_hz: scans per second
    double coneDeg = 80.0;          // combat.auto_lock_cone_deg: half angle around the nose
    float pause = 3.0f;             // combat.auto_lock_pause: seconds without auto-lock after a manual clear (Y)
    bool instant = false;           // combat.auto_lock_instant: a launch without a lock locks the nearest at once (skips the acquisition time)
};
struct AutoLock { float scanTimer = 0; float pause = 0; };

// Nearest candidate inside range and cone (behind the ship never counts). Ties (within 1e-6 units) go to the lower id. Returns an index into c, -1 = none.
inline int nearestInCone(const Vec3d& shipPos, const Vec3d& fwd, const std::vector<LockCandidate>& c, double range, double coneDeg) {
    int best = -1; double bd = 0;
    for (int i = 0; i < (int)c.size(); i++) {
        Vec3d r = sub(c[i].pos, shipPos);
        double d = length(r);
        if (d > range || angleDeg(fwd, r) > coneDeg) continue;
        if (best < 0 || d < bd - 1e-6 || (std::fabs(d - bd) <= 1e-6 && c[i].id < c[best].id)) { best = i; bd = d; }
    }
    return best;
}

// A manual clear (Y, or T on the only target) suspends auto-lock for p.pause seconds.
inline void autoLockManualClear(AutoLock& a, const AutoLockParams& p) { a.pause = std::max(0.0f, p.pause); }

// Rate limiter: advances the timers and says whether a scan should run this step (enabled, missile selected, no lock held, not paused, due).
inline bool autoLockDue(AutoLock& a, const AutoLockParams& p, float dt, bool missileSelected, LockState s) {
    if (a.pause > 0) a.pause = std::max(0.0f, a.pause - dt);
    if (a.scanTimer > 0) a.scanTimer -= dt;
    if (!p.enabled || !missileSelected || a.pause > 0) return false;
    if (s == LockState::Acquiring || s == LockState::Locked) return false;   // hysteresis: never hop while a lock exists
    if (a.scanTimer > 0) return false;
    a.scanTimer = p.hz > 0 ? 1.0f / p.hz : 0.0f;
    return true;
}

// Starts acquiring an auto-picked target (instant = locked at once).
inline void startAutoLock(Lock& l, int id, bool instant) {
    l = Lock{};
    l.state = instant ? LockState::Locked : LockState::Acquiring; l.target = id; l.autoPicked = true;
}

// ---- guidance ----
// MissileParams (the tunables of one missile) lives in weapons_rules.h: it is part of a WeaponDef.

// The proportional-navigation command: a = N * Vc * (Omega x LOS), Omega = the LOS rotation rate. Perpendicular to the line of sight.
// Vc is kept at a minimum (a missile that is not closing yet still turns towards the target instead of away from it).
inline Vec3d pnAccel(const Vec3d& mPos, const Vec3d& mVel, const Vec3d& tPos, const Vec3d& tVel, double N) {
    Vec3d r = sub(tPos, mPos), vr = sub(tVel, mVel);
    double r2 = dot(r, r);
    if (r2 < 1e-9) return {0, 0, 0};
    double rl = std::sqrt(r2);
    Vec3d los = mul(r, 1.0 / rl);
    Vec3d omega = mul(cross(r, vr), 1.0 / r2);
    double vc = std::max(-dot(r, vr) / rl, 20.0);
    return mul(cross(omega, los), N * vc);
}

// Where the nose should point: the PN lateral command plus the rest of the thrust towards the target (the missile keeps closing).
// A command larger than the thrust is clamped: then the whole thrust goes sideways.
inline Vec3d desiredHeading(const Vec3d& mPos, const Vec3d& mVel, const Vec3d& tPos, const Vec3d& tVel, const MissileParams& p) {
    Vec3d a = pnAccel(mPos, mVel, tPos, tVel, p.navGain);
    Vec3d los = normalized(sub(tPos, mPos));
    double al = length(a), T = std::max(1e-6, p.thrust);
    if (al >= T) return mul(a, 1.0 / al);
    return normalized(add(a, mul(los, std::sqrt(T * T - al * al))), los);
}

// Turns `h` (unit) towards `want` (unit) by at most maxRad; returns the new unit heading.
inline Vec3d turnToward(const Vec3d& h, const Vec3d& want, double maxRad) {
    double c = std::clamp(dot(h, want), -1.0, 1.0), ang = std::acos(c);
    if (ang <= maxRad || ang < 1e-9) return want;
    Vec3d axis = cross(h, want);
    double al = length(axis);
    if (al < 1e-9) {                                                 // exactly opposite: pick any perpendicular axis
        axis = cross(h, std::fabs(h.y) < 0.9 ? Vec3d{0, 1, 0} : Vec3d{1, 0, 0});
        al = length(axis);
    }
    axis = mul(axis, 1.0 / al);
    // Rodrigues: rotate h about axis by maxRad (h is perpendicular to axis)
    Vec3d r = add(mul(h, std::cos(maxRad)), mul(cross(axis, h), std::sin(maxRad)));
    return normalized(r, want);
}

struct MissileState {
    Vec3d pos, vel, heading{0, 0, -1};
    float fuel = 0, life = 0;
    double travelled = 0;
};

inline bool isArmed(const MissileState& m, const MissileParams& p) { return m.travelled >= p.armDistance; }

// One step: steer (only with a target and fuel: a coasting missile has no control), thrust while fuel lasts, move. Semi-implicit Euler.
inline void stepMissile(MissileState& m, const MissileParams& p, bool hasTarget, const Vec3d& tPos, const Vec3d& tVel, double dt) {
    if (dt <= 0) return;
    bool burning = m.fuel > 0.0f;
    if (hasTarget && burning) m.heading = turnToward(m.heading, desiredHeading(m.pos, m.vel, tPos, tVel, p), p.turnRateDeg * kPi / 180.0 * dt);
    if (burning) {
        double burn = std::min((double)m.fuel, dt);                  // the last partial step burns only what is left
        m.vel = add(m.vel, mul(m.heading, p.thrust * burn));
        m.fuel = std::max(0.0f, m.fuel - (float)dt);
    }
    Vec3d np = add(m.pos, mul(m.vel, dt));
    m.travelled += length(sub(np, m.pos));
    m.pos = np;
    m.life -= (float)dt;
}

// The launch state: the ship's velocity plus a kick along the nose; full fuel and lifetime.
inline MissileState launchMissile(const Vec3d& muzzle, const Vec3d& shipVel, const Vec3d& fwd, const MissileParams& p) {
    MissileState m;
    m.pos = muzzle; m.heading = normalized(fwd); m.vel = add(shipVel, mul(m.heading, p.launchKick));
    m.fuel = p.fuel; m.life = p.lifetime; m.travelled = 0;
    return m;
}

// Blast damage at `surfaceDist` from the explosion (0 = touching): max at 0, linear to 0 at the blast radius.
inline float blastDamage(double surfaceDist, const MissileParams& p) {
    if (p.blastRadius <= 0) return 0.0f;
    double k = 1.0 - std::max(0.0, surfaceDist) / p.blastRadius;
    return k <= 0 ? 0.0f : (float)(p.maxDamage * k);
}

// ---- the missile pool ----
struct MissilePool {
    std::vector<double> px, py, pz, vx, vy, vz, hx, hy, hz, travelled;
    std::vector<float> fuel, life;
    std::vector<int> target, shooter;
    int n = 0, capacity = 0;
    long dropped = 0;

    void init(int cap) {
        capacity = std::max(0, cap);
        for (auto* v : {&px, &py, &pz, &vx, &vy, &vz, &hx, &hy, &hz, &travelled}) v->assign(capacity, 0.0);
        fuel.assign(capacity, 0); life.assign(capacity, 0); target.assign(capacity, -1); shooter.assign(capacity, 0);
        n = 0; dropped = 0;
    }
    MissileState get(int i) const {
        MissileState m;
        m.pos = {px[i], py[i], pz[i]}; m.vel = {vx[i], vy[i], vz[i]}; m.heading = {hx[i], hy[i], hz[i]};
        m.fuel = fuel[i]; m.life = life[i]; m.travelled = travelled[i];
        return m;
    }
    void set(int i, const MissileState& m) {
        px[i] = m.pos.x; py[i] = m.pos.y; pz[i] = m.pos.z; vx[i] = m.vel.x; vy[i] = m.vel.y; vz[i] = m.vel.z;
        hx[i] = m.heading.x; hy[i] = m.heading.y; hz[i] = m.heading.z; fuel[i] = m.fuel; life[i] = m.life; travelled[i] = m.travelled;
    }
    bool spawn(const MissileState& m, int tgt, int who) {
        if (n >= capacity) { dropped++; return false; }
        int i = n++;
        set(i, m); target[i] = tgt; shooter[i] = who;
        return true;
    }
    void remove(int i) {
        int last = --n;
        if (i != last) { set(i, get(last)); target[i] = target[last]; shooter[i] = shooter[last]; }
    }
};

} // namespace combat
