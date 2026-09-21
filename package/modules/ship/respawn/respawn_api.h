#pragma once
// ship/respawn: after the ship dies, waits a few seconds and calls ship::IShip::respawn().
// Other modules (the HUD) ask it how long is left so they can show a countdown:
//     auto* r = eng.services.get<ship::IRespawn>();     // null if the module is off
//     if (r && r->counting()) show("RESPAWNING IN " + ceil(r->secondsLeft()));
namespace ship {

class IRespawn {
public:
    virtual ~IRespawn() = default;
    virtual bool counting() const = 0;          // a countdown is running (the ship is dead and respawn is enabled)
    virtual float secondsLeft() const = 0;      // seconds until the ship respawns; 0 when not counting
};

} // namespace ship
