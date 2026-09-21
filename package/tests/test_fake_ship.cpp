// ship/fake_ship must win over the real ship only when --fake-ship is given. The fake's source is included so it registers in the
// test binary; "ship/ship_core" below is a stand-in for the real provider (same module name, so the fake's optional dependency orders it).
#include "ship/fake_ship/fake_ship.cpp"
#include "tests/test.h"

namespace {
float g_seenHp = -1;
int g_probeRuns = 0;

class StubRealShip : public engine::Module, public ship::IShip {
public:
    const char* name() const override { return "ship/ship_core"; }
    bool init(engine::Engine& eng) override { eng.services.provide<ship::IShip>(this); return true; }
    void shutdown(engine::Engine& eng) override { eng.services.withdraw<ship::IShip>(); }
    const ship::ShipStatus& status() const override { return st_; }
    engine::Vec3 position() const override { return {}; }
    engine::Vec3 velocity() const override { return {}; }
    engine::Vec3 forward() const override { return {}; }
    void applyDamage(float, const std::string&) override {}
    void heal(float) override {}
    void addWarpFuel(float) override {}
    bool consumeWarpFuel(float) override { return true; }
    void addMaxHp(float) override {}
    void installShield(bool) override {}
    void setVelocity(const engine::Vec3&) override {}
    void kill(const std::string&) override {}
    void respawn() override {}
    ship::ShipStatus st_;   // hp 100
};
REGISTER_MODULE(StubRealShip);

// runs after both providers and records which one is visible
class ShipProbe : public engine::Module {
public:
    const char* name() const override { return "t/ship_probe"; }
    std::vector<std::string> optionalDependencies() const override { return {"ship/fake_ship", "ship/ship_core"}; }
    bool init(engine::Engine& eng) override {
        if (auto* s = eng.services.get<ship::IShip>()) { g_seenHp = s->status().hp; g_probeRuns++; }
        return true;
    }
};
REGISTER_MODULE(ShipProbe);

int runFake(std::vector<std::string> flags) {
    std::vector<std::string> args = {"test", "--frames=2", "--disable=core/input_handler"};
    for (auto& f : flags) args.push_back(f);
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    engine::Engine e;
    return e.run((int)argv.size(), argv.data());
}
} // namespace

TEST(fake_ship_declares_it_inits_after_the_real_ship) {
    FakeShip f;
    auto deps = f.optionalDependencies();
    CHECK(std::find(deps.begin(), deps.end(), "ship/ship_core") != deps.end());
}

TEST(fake_ship_overrides_the_real_ship_only_with_the_flag) {
    g_seenHp = -1; g_probeRuns = 0;
    CHECK_EQ(runFake({"--fake-ship=hp:35,shield:120,fuel:10"}), 0);   // also proves shutdown order is safe
    CHECK_EQ(g_probeRuns, 1);
    CHECK_EQ(g_seenHp, 35.0f);

    g_seenHp = -1; g_probeRuns = 0;
    CHECK_EQ(runFake({}), 0);
    CHECK_EQ(g_seenHp, 100.0f);                                        // inert: the real ship stays

    g_seenHp = -1;
    CHECK_EQ(runFake({"--fake-ship"}), 0);                             // bare flag without values: still inert
    CHECK_EQ(g_seenHp, 100.0f);
}
