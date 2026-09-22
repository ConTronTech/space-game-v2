// Tests for core/debugger's logic (debugger_rules.h): watch registry, grouping by prefix, the log ring, draw-hook toggles, the dump format.
#include <functional>
#include <stdexcept>
#include "core/debugger/debugger_rules.h"
#include "tests/test.h"

using namespace dbg;

TEST(debugger_watch_register_poll_unwatch) {
    WatchRegistry w;
    int n = 0;
    CHECK(w.add("ship.speed", [&] { return std::to_string(++n); }));
    CHECK_EQ(w.value("ship.speed"), std::string("-"));            // not polled yet
    w.poll();
    CHECK_EQ(w.value("ship.speed"), std::string("1"));
    w.poll();
    CHECK_EQ(w.value("ship.speed"), std::string("2"));
    CHECK(w.remove("ship.speed"));
    CHECK(!w.remove("ship.speed"));
    w.poll();                                                    // a later poll after unwatch: nothing called, no crash
    CHECK_EQ(n, 2);
    CHECK_EQ(w.value("ship.speed"), std::string(""));
    CHECK_EQ((int)w.size(), 0);
}

TEST(debugger_watch_collision_replaces_and_errors_are_caught) {
    WatchRegistry w;
    CHECK(w.add("a.x", [] { return std::string("old"); }));
    CHECK(!w.add("a.x", [] { return std::string("new"); }));    // same name: replaced, still one entry
    CHECK_EQ((int)w.size(), 1);
    w.add("a.bad", []() -> std::string { throw std::runtime_error("boom"); });
    w.add("a.null", nullptr);
    w.poll();
    CHECK_EQ(w.value("a.x"), std::string("new"));
    CHECK_EQ(w.value("a.bad"), std::string("<error: boom>"));
    CHECK_EQ(w.value("a.null"), std::string("-"));
}

TEST(debugger_grouping_by_prefix) {
    CHECK_EQ(groupOf("gravity.dominant"), std::string("gravity"));
    CHECK_EQ(leafOf("gravity.accel.x"), std::string("accel.x"));
    CHECK_EQ(groupOf("loose"), std::string("misc"));
    CHECK_EQ(groupOf(".odd"), std::string("misc"));
    WatchRegistry w;
    for (const char* n : {"orbit.state", "gravity.dominant", "gravity.accel", "loose"}) w.add(n, [] { return std::string("v"); });
    w.poll();
    auto g = groupByPrefix(w.values());
    CHECK_EQ((int)g.size(), 3);
    CHECK_EQ(g[0].name, std::string("gravity"));
    CHECK_EQ((int)g[0].rows.size(), 2);
    CHECK_EQ(g[0].rows[0].first, std::string("accel"));          // sorted by full name
    CHECK_EQ(g[1].name, std::string("misc"));
    CHECK_EQ(g[2].name, std::string("orbit"));
}

TEST(debugger_log_ring_capacity_and_order) {
    LogRing r(3);
    CHECK_EQ((int)r.size(), 0);
    for (int i = 1; i <= 5; i++) r.push("l" + std::to_string(i));
    CHECK_EQ((int)r.size(), 3);
    CHECK_EQ((int)r.total(), 5);
    CHECK_EQ(r.at(0), std::string("l3"));                         // oldest kept
    CHECK_EQ(r.at(2), std::string("l5"));
    auto all = r.all();
    CHECK_EQ((int)all.size(), 3);
    CHECK_EQ(all[1], std::string("l4"));
    LogRing z(0);                                                 // capacity is at least 1
    z.push("a"); z.push("b");
    CHECK_EQ((int)z.size(), 1);
    CHECK_EQ(z.at(0), std::string("b"));
}

TEST(debugger_draw_hook_toggles) {
    DrawHooks<std::function<void()>> h;
    int calls = 0;
    CHECK(h.add("physics.velocity", [&] { calls++; }));
    CHECK(!h.enabled("physics.velocity"));                        // off by default
    h.setEnabled("physics.velocity", true);
    CHECK_EQ(h.enabledCount(), 1);
    h.forEachEnabled([](const std::string&, const std::function<void()>& f) { f(); });
    CHECK_EQ(calls, 1);
    CHECK(h.remove("physics.velocity"));
    CHECK_EQ(h.enabledCount(), 0);
    h.forEachEnabled([](const std::string&, const std::function<void()>& f) { f(); });
    CHECK_EQ(calls, 1);
    h.add("physics.velocity", [&] { calls += 10; });              // re-registered: keeps the player's "on"
    CHECK(h.enabled("physics.velocity"));
    h.setEnabled("later.hook", true);                             // switched on before it exists (--debug-draw)
    h.add("later.hook", [] {});
    CHECK(h.enabled("later.hook"));
    h.setEnabled("later.hook", false);
    CHECK(!h.enabled("later.hook"));
    CHECK(!h.enabled("missing"));
}

TEST(debugger_dump_round_trip) {
    DumpData d;
    d.header = {{"build", "today"}, {"uptime", "12.00 s (frame 700)"}};
    d.watches = {{"gravity.accel", "(1.0, 2.0, 3.0)"}, {"gravity.dominant", "Planet 1"}, {"loose", "x = y"}, {"ship.speed", "4.5 m/s"}};
    d.hooks = {{"physics.velocity", true}, {"gravity.accel", false}};
    d.log = {"t=     1.00  ship: respawned", "t=     2.00  multi\nline"};
    std::string text = formatDump(d);
    CHECK(text.find("# SPACE GAME V2 - DEBUG STATE SNAPSHOT") == 0);
    CHECK(text.find("F5") != std::string::npos);                  // points at the profiler for performance
    DumpData p = parseDump(text);
    CHECK_EQ((int)p.header.size(), 2);
    CHECK_EQ(p.header[1].second, std::string("12.00 s (frame 700)"));
    CHECK_EQ((int)p.watches.size(), 4);
    for (size_t i = 0; i < p.watches.size() && i < d.watches.size(); i++) {
        CHECK_EQ(p.watches[i].first, d.watches[i].first);
        CHECK_EQ(p.watches[i].second, d.watches[i].second);
    }
    CHECK_EQ((int)p.hooks.size(), 2);
    CHECK(p.hooks[0].second && !p.hooks[1].second);
    CHECK_EQ((int)p.log.size(), 2);
    CHECK_EQ(p.log[1], std::string("t=     2.00  multi line"));   // newlines flattened: one event, one line
}
