#include "engine/event_bus.h"
#include "engine/services.h"
#include "tests/test.h"

struct Ping { int n; };
struct Pong { int n; };
struct Svc { int v = 5; };

TEST(eventbus_delivers_to_matching_type_only) {
    engine::EventBus bus;
    int pings = 0, pongs = 0;
    bus.subscribe<Ping>([&](const Ping& p) { pings += p.n; });
    bus.subscribe<Ping>([&](const Ping& p) { pings += p.n * 10; }); // second handler also runs
    bus.subscribe<Pong>([&](const Pong&) { pongs++; });
    bus.emit(Ping{2});
    CHECK_EQ(pings, 22);
    CHECK_EQ(pongs, 0);
    bus.emit(Pong{1});
    CHECK_EQ(pongs, 1);
}

TEST(eventbus_emit_without_subscribers_is_fine) {
    engine::EventBus bus;
    bus.emit(Ping{1});
    CHECK(true);
}

TEST(services_provide_get_withdraw) {
    engine::Services s;
    Svc svc;
    CHECK(s.get<Svc>() == nullptr);
    s.provide<Svc>(&svc);
    CHECK(s.get<Svc>() == &svc);
    CHECK_EQ(s.require<Svc>().v, 5);
    s.withdraw<Svc>();
    CHECK(s.get<Svc>() == nullptr);
}

TEST(services_require_throws_when_missing) {
    engine::Services s;
    bool threw = false;
    try { s.require<Svc>(); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}
