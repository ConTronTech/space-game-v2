// InputHandler action logic driven by a fake device (no SDL, no window).
#include "core/input_handler/input_handler.h"
#include "engine/engine.h"
#include "tests/test.h"

struct FakeDevice : core::InputMethod {
    float level = 0;
    const char* device() const override { return "fake"; }
    bool addBinding(const std::string&, const engine::Json&, std::string&) override { return true; }
    void clearBindings() override {}
    void poll(core::InputHandler& in) override { if (level != 0) in.contribute("act", level); }
};

struct Rig {
    engine::Engine eng;
    core::InputHandler in;
    FakeDevice dev;
    Rig() {
        in.init(eng);
        in.registerMethod(&dev);
    }
    void frame() { in.onFrameBegin(eng); }
};

TEST(input_held_at_startup_is_not_a_fresh_press) {
    Rig r;
    r.dev.level = 1;
    r.frame();
    CHECK(r.in.down("act"));
    CHECK(!r.in.pressed("act"));   // was already held when the game started
    r.frame();
    CHECK(!r.in.pressed("act"));
}

TEST(input_press_and_release_edges) {
    Rig r;
    r.frame();                      // first frame settles
    r.dev.level = 1; r.frame();
    CHECK(r.in.pressed("act"));
    CHECK(r.in.down("act"));
    r.frame();
    CHECK(!r.in.pressed("act"));    // only the frame it went down
    CHECK(r.in.down("act"));
    r.dev.level = 0; r.frame();
    CHECK(r.in.released("act"));
    CHECK(!r.in.down("act"));
    r.frame();
    CHECK(!r.in.released("act"));
}

TEST(input_sources_sum_and_clamp) {
    Rig r;
    r.frame();
    r.dev.level = 0.75f;
    r.in.contribute("other", 0.75f);
    r.frame();
    CHECK_EQ(r.in.value("act"), 0.75f);
    r.in.contribute("act", 0.75f);   // a second source in the same frame
    CHECK_EQ(r.in.value("act"), 1.0f);      // 1.5 clamped
    r.in.contribute("neg", -3.0f);
    CHECK_EQ(r.in.value("neg"), -1.0f);
    CHECK(r.in.down("neg"));                // negative deflection counts as held
}

TEST(input_unknown_action_reads_zero) {
    Rig r;
    r.frame();
    CHECK_EQ(r.in.value("nope"), 0.0f);
    CHECK(!r.in.down("nope"));
    CHECK(!r.in.pressed("nope"));
}

TEST(input_below_threshold_is_not_down) {
    Rig r;
    r.frame();
    r.dev.level = 0.3f; r.frame();
    CHECK(!r.in.down("act"));
    CHECK_EQ(r.in.value("act"), 0.3f);
}
