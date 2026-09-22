#pragma once
// Pure logic for core/input_methods/agent: parsing the command file and building the status JSON. No GL/SDL/engine
// module types - engine::Json itself is a plain data type used the same way other "_data.h" parsers use it.
// Unit-tested in package/tests/test_agent_input.cpp.
#include <string>
#include <unordered_map>
#include <vector>
#include "engine/json.h"

namespace agent {

// The command file is a flat object of action -> number, e.g. {"thrust": 1.0, "fire": 1.0}. Held every frame until
// the file changes (like a real device continuously reporting its current state) - see AgentInput::poll(). Non-numeric
// values and a non-object file are ignored (returns empty, never throws); this is deliberately forgiving since a
// human (or another process) may be mid-edit of the file when it gets polled.
inline std::unordered_map<std::string, float> parseCommands(const engine::Json& j) {
    std::unordered_map<std::string, float> out;
    if (!j.isObject()) return out;
    for (auto& k : j.keys()) out[k] = (float)j[k].num(0.0);   // a non-numeric value quietly becomes 0 (harmless: 0 thrust/fire/etc does nothing)
    return out;
}

// One asteroid entry in the status snapshot's "nearby ore" sensor readout.
struct AsteroidReport { int id = -1; std::string ore; double distance = 0; float radius = 0; double x = 0, y = 0, z = 0; };
// One ore stack (id -> count) for the cargo summary.
struct OreStack { std::string id; int count = 0; };

struct ShipReport {
    float hp = 0, maxHp = 0, shield = 0, maxShield = 0, warpFuel = 0, maxWarpFuel = 0, speed = 0;
    bool alive = false, warping = false;
    double x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
};
struct DockReport { bool have = false, docked = false, ok = false; std::string stationName, nearestName, reason; float distance = 0; };
struct CargoReport { float used = 0, capacity = 0; std::vector<OreStack> stacks; std::vector<std::string> perks; };

// Builds the status JSON written to agent.status_file. Plain data in, engine::Json out - no service calls here (the
// module gathers the data by calling services, this function only assembles it, so it stays unit-testable).
inline engine::Json buildStatus(long frame, double simTime, const ShipReport& ship, const DockReport& dock, const CargoReport& cargo,
                                 const std::vector<AsteroidReport>& asteroids) {
    auto vec3 = [](double x, double y, double z) { return engine::Json::array().push(x).push(y).push(z); };
    engine::Json j = engine::Json::object()
        .set("frame", (double)frame).set("time", simTime)
        .set("ship", engine::Json::object()
            .set("alive", ship.alive).set("hp", ship.hp).set("maxHp", ship.maxHp)
            .set("shield", ship.shield).set("maxShield", ship.maxShield)
            .set("warpFuel", ship.warpFuel).set("maxWarpFuel", ship.maxWarpFuel)
            .set("warping", ship.warping).set("speed", ship.speed)
            .set("position", vec3(ship.x, ship.y, ship.z)).set("velocity", vec3(ship.vx, ship.vy, ship.vz)));
    if (dock.have)
        j.set("docking", engine::Json::object().set("docked", dock.docked).set("stationName", dock.stationName)
                   .set("nearestName", dock.nearestName).set("distance", (double)dock.distance).set("ok", dock.ok).set("reason", dock.reason));
    engine::Json stacks = engine::Json::array();
    for (auto& s : cargo.stacks) stacks.push(engine::Json::object().set("id", s.id).set("count", (double)s.count));
    engine::Json perks = engine::Json::array();
    for (auto& p : cargo.perks) perks.push(p);
    j.set("cargo", engine::Json::object().set("used", cargo.used).set("capacity", cargo.capacity).set("stacks", stacks).set("perks", perks));
    engine::Json rocks = engine::Json::array();
    for (auto& a : asteroids)
        rocks.push(engine::Json::object().set("id", (double)a.id).set("ore", a.ore).set("distance", a.distance)
                       .set("radius", (double)a.radius).set("position", vec3(a.x, a.y, a.z)));
    j.set("nearbyAsteroids", rocks);
    return j;
}

} // namespace agent
