// InputHandler action logic driven by a fake device (no SDL, no window).
#include <map>
#include "core/input_handler/input_handler.h"
#include "engine/engine.h"
#include "tests/test.h"

namespace {
struct FakeDevice : core::InputMethod {
    float level = 0;
    const char* device() const override { return "fake"; }
    bool addBinding(const std::string&, const engine::Json&, std::string&) override { return true; }
    void clearBindings() override {}
    void poll(core::IInput& in) override { if (level != 0) in.contribute("act", level); }
};

// A device that names its bindings from a fixed table (for primaryBindingLabel).
struct LabelDevice : core::InputMethod {
    const char* dev;
    std::map<std::string, std::string> labels;
    explicit LabelDevice(const char* d) : dev(d) {}
    const char* device() const override { return dev; }
    bool addBinding(const std::string&, const engine::Json&, std::string&) override { return true; }
    void clearBindings() override {}
    void poll(core::IInput&) override {}
    std::string bindingLabel(const std::string& a) const override { auto it = labels.find(a); return it == labels.end() ? std::string() : it->second; }
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
} // namespace

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

TEST(input_consume_zeroes_the_rest_of_this_frame_only) {
    Rig r;
    r.dev.level = 1; r.frame();
    CHECK(r.in.down("act"));
    r.in.consume("act");
    CHECK(!r.in.down("act"));
    CHECK_EQ(r.in.value("act"), 0.0f);
    r.frame();                      // next frame: the device still contributes, consume() does not stick
    CHECK(r.in.down("act"));
}

TEST(input_consume_beats_a_manual_offset_when_two_sources_press_the_same_action) {
    // the bug a raw contribute(action, -1) offset could hide: two sources both pressing "pause" the same frame sum to
    // more than 1 before consume(); consume() zeroes it outright regardless of how many sources contributed.
    Rig r;
    r.frame();
    r.in.contribute("pause", 1.0f);
    r.in.contribute("pause", 1.0f);   // e.g. keyboard AND a wheel button both pressed this frame
    CHECK(r.in.down("pause"));
    r.in.consume("pause");
    CHECK(!r.in.down("pause"));
}

TEST(input_binding_label_prefers_keyboard_then_mouse_then_joystick_then_others) {
    Rig r;                                              // its "fake" device names nothing (InputMethod's default)
    LabelDevice kb("keyboard"), mouse("mouse"), joy("joystick"), zz("zz_agent"), aa("aa_wheel");
    for (auto* d : {&zz, &aa, &joy, &mouse, &kb}) r.in.registerMethod(d);
    CHECK_EQ(r.in.primaryBindingLabel("dock"), std::string(""));   // nothing bound anywhere: caller picks a fallback
    zz.labels["dock"] = "Z"; aa.labels["dock"] = "A";
    CHECK_EQ(r.in.primaryBindingLabel("dock"), std::string("A"));  // unknown devices: alphabetical, not hash order
    joy.labels["dock"] = "Button 3";
    CHECK_EQ(r.in.primaryBindingLabel("dock"), std::string("Button 3"));
    mouse.labels["dock"] = "Right Click";
    CHECK_EQ(r.in.primaryBindingLabel("dock"), std::string("Right Click"));
    kb.labels["dock"] = "G";
    CHECK_EQ(r.in.primaryBindingLabel("dock"), std::string("G"));  // keyboard wins
    CHECK_EQ(r.in.primaryBindingLabel("fire"), std::string(""));   // other actions unaffected
    r.in.unregisterMethod(&kb);
    CHECK_EQ(r.in.primaryBindingLabel("dock"), std::string("Right Click"));
}

TEST(input_mouse_labels_are_readable) {
    CHECK_EQ(core::input_label::mouseButton(1), std::string("Left Click"));
    CHECK_EQ(core::input_label::mouseButton(2), std::string("Middle Click"));
    CHECK_EQ(core::input_label::mouseButton(3), std::string("Right Click"));
    CHECK_EQ(core::input_label::mouseButton(5), std::string("Mouse 5"));
    CHECK_EQ(core::input_label::mouseButton(9), std::string(""));
    CHECK_EQ(core::input_label::mouseAxis(2), std::string("Mouse Wheel"));
}
