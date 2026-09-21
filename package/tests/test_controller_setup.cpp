#include <cmath>
#include "core/input_methods/joystick/joystick_profile.h"
#include "core/input_methods/joystick/joystick_rules.h"
#include "engine/json.h"
#include "tests/test.h"
#include "ui/controller_setup/controller_setup_rules.h"

namespace {
using namespace csetup;
bool near(double a, double b, double tol = 0.02) { return std::fabs(a - b) <= tol; }

// Runs the whole default wizard against the simulated PXN V10 (optional steps other than the clutch are skipped), like --controller-setup=demo.
Wizard runPxn(bool withClutch = true) {
    Wizard w;
    Snapshot s;
    for (int guard = 0; guard < 2000 && !w.finished(); guard++) {
        size_t step = w.step();
        const StepDef& d = w.current();
        if (d.optional && (std::string(d.key) != "clutch" || !withClutch)) { w.skip(); continue; }
        for (int t = 0; t < 40 && w.step() == step; t++) { simulatePxn(d, t, s); w.feed(s); if (t >= 30) w.confirm(); }
    }
    return w;
}
const StepResult& resultOf(const Wizard& w, const char* key) {
    for (size_t i = 0; i < w.steps().size(); i++) if (std::string(w.steps()[i].key) == key) return w.results()[i];
    static StepResult none; return none;
}
bool parseText(const std::string& text, joystick::Profile& p, std::string& err) {
    engine::Json j = engine::Json::parse(text, &err);
    return j.isObject() && joystick::parseProfile(j, p, err);
}
float sum(const std::vector<joystick::Contribution>& c, const char* a) { float v = 0; for (auto& x : c) if (x.action == a) v += x.value; return v; }
} // namespace

TEST(controller_setup_wizard_detects_the_pxn_controls_by_change) {
    Wizard w = runPxn();
    CHECK(w.finished());
    CHECK_EQ(resultOf(w, "steering").index, 0);
    CHECK(resultOf(w, "steering").held > 16000);                                 // right = + on this wheel
    CHECK_EQ(resultOf(w, "throttle").index, 2);
    CHECK_EQ(resultOf(w, "brake").index, 5);
    CHECK_EQ(resultOf(w, "clutch").index, 6);
    CHECK(restsLow(resultOf(w, "throttle")) && restsLow(resultOf(w, "brake")) && restsLow(resultOf(w, "clutch")));
    CHECK_EQ(resultOf(w, "fire").index, 0);
    CHECK_EQ(resultOf(w, "dock").index, 13);
    CHECK_EQ(resultOf(w, "warp").index, 3);
    CHECK_EQ(resultOf(w, "orbit_lock").index, 9);
    CHECK_EQ(resultOf(w, "menu").index, 12);
    CHECK_EQ(resultOf(w, "camera").index, 8);
    CHECK_EQ(resultOf(w, "weapon_next").index, 1);
    CHECK(resultOf(w, "weapon_prev").skipped && resultOf(w, "lock_target").skipped && resultOf(w, "lock_clear").skipped);
}

TEST(controller_setup_wizard_ignores_a_wrong_first_value) {
    // steering reads +32767 (wrong) at the start, then the device reports its real centre: that jump is not the answer
    Wizard w;
    Snapshot s; s.axes.assign(8, 0); s.buttons.assign(8, 0);
    s.axes[0] = 32767;
    for (int i = 0; i < Wizard::kSettleFrames + 2; i++) w.feed(s);
    s.axes[0] = 0; w.feed(s);
    CHECK(!w.pending());
    s.axes[0] = 30000; w.feed(s);                                                // now the user turns right
    CHECK(w.pending() && w.ready());
    CHECK_EQ(w.currentResult().index, 0);
    CHECK(w.currentResult().held > 0);
    // a wrong first value that arrives during the settle frames is simply absorbed into the baseline
    Wizard w2; Snapshot t; t.axes.assign(4, 0); t.buttons.assign(4, 0);
    t.axes[1] = -32768; w2.feed(t); t.axes[1] = 0; for (int i = 0; i < Wizard::kSettleFrames + 3; i++) w2.feed(t);
    CHECK(!w2.pending());
}

TEST(controller_setup_wizard_confirm_retry_back_skip) {
    Wizard w;
    Snapshot s; s.axes.assign(8, 0); s.buttons.assign(16, 0);
    for (int i = 0; i < 10; i++) w.feed(s);
    CHECK(!w.skip());                                                            // steering is required
    CHECK(!w.confirm());                                                         // nothing detected yet
    s.axes[3] = -25000; w.feed(s);
    CHECK(w.pending());
    CHECK(near(w.currentResult().held, -25000, 1));
    w.retry();                                                                   // BACKSPACE: ask again
    CHECK(!w.pending());
    s.axes[3] = 0; for (int i = 0; i < 10; i++) w.feed(s);
    s.axes[0] = 28000; w.feed(s);
    CHECK(w.confirm());
    CHECK_EQ(w.step(), (size_t)1);
    // the used axis is ignored for the next step even if it moves again
    for (int i = 0; i < 10; i++) w.feed(s);
    s.axes[0] = -28000; w.feed(s);
    CHECK(!w.pending());
    w.back();                                                                    // back to steering
    CHECK_EQ(w.step(), (size_t)0);
    CHECK(!w.results()[0].done);
    // a pedal half pressed is a candidate but not confirmable until it is held down
    Wizard p; Snapshot q; q.axes.assign(4, -32768); q.buttons.assign(4, 0); q.axes[0] = 0;
    for (int i = 0; i < 10; i++) p.feed(q);
    q.axes[0] = 25000; p.feed(q); p.confirm();                                   // steering
    for (int i = 0; i < 10; i++) p.feed(q);
    q.axes[2] = 8000; p.feed(q);
    CHECK(p.pending() && !p.ready());
    q.axes[2] = 32767; p.feed(q);
    CHECK(p.ready());
}

TEST(controller_setup_wizard_buttons_skip_and_finish) {
    std::vector<StepDef> steps = {{"fire", "", Kind::Button, false, "fire"}, {"lock", "", Kind::Button, true, "lock_target"}, {"dock", "", Kind::Button, false, "dock"}};
    Wizard w(steps);
    Snapshot s; s.axes.assign(2, 0); s.buttons.assign(8, 0);
    s.buttons[5] = 1;                                                            // held since before the step: must be released first
    for (int i = 0; i < 10; i++) w.feed(s);
    CHECK(!w.pending());
    s.buttons[5] = 0; w.feed(s); s.buttons[5] = 1; w.feed(s);
    CHECK(w.pending());
    CHECK_EQ(w.currentResult().index, 5);
    CHECK(w.confirm());
    CHECK(w.skip());                                                             // optional
    s.buttons[5] = 0; for (int i = 0; i < 10; i++) w.feed(s);
    s.buttons[5] = 1; w.feed(s);
    CHECK(!w.pending());                                                         // button 5 is fire already
    s.buttons[2] = 1; w.feed(s);
    CHECK(w.confirm());
    CHECK(w.finished());
    CHECK(w.results()[1].skipped);
}

TEST(controller_setup_profile_generator_pedal_rest_and_steering_invert) {
    std::vector<StepDef> steps = {defaultSteps()[0], defaultSteps()[1], defaultSteps()[2], defaultSteps()[3]};
    std::vector<StepResult> r(4);
    r[0].done = true; r[0].index = 1; r[0].baseline = 0; r[0].held = -30000;      // this wheel reads - to the right: no invert
    r[1].done = true; r[1].index = 2; r[1].baseline = 32767; r[1].held = -32768; r[1].minSeen = -32768; r[1].maxSeen = 32767;   // rests HIGH
    r[2].done = true; r[2].index = 3; r[2].baseline = -32768; r[2].held = 28000; r[2].minSeen = -32768; r[2].maxSeen = 28000;  // short of the stop
    r[3].skipped = true;
    std::string text = buildProfile("Test Wheel", 0x1234, 0xabcd, steps, r, "2026-09-21");
    joystick::Profile p; std::string err;
    CHECK(parseText(text, p, err));
    CHECK_EQ(p.axes.size(), (size_t)3);
    CHECK(!p.axes[0].invert);
    CHECK(p.axes[0].role == joystick::Role::Steering);
    CHECK(near(p.axes[0].deadzone, 0.03, 1e-4) && near(p.axes[0].curve, 1.5, 1e-4));
    CHECK(p.axes[1].rest == joystick::Rest::Max);
    CHECK(p.axes[2].rest == joystick::Rest::Min);
    CHECK_EQ(p.axes[2].cal.max, 28000);                                          // calibrated so a full press still reads 1
    CHECK(near(joystick::axisValue(p.axes[2], 28000, 1.0f), 1.0));
    CHECK(p.buttons.empty());
    CHECK(text.find("GENERATED 2026-09-21") != std::string::npos);
    CHECK(text.find("clutch: skipped") != std::string::npos);
    CHECK_EQ(p.match.vid, 0x1234);
    CHECK_EQ(p.match.pid, 0xabcd);
}

TEST(controller_setup_generated_pxn_profile_parses_and_drives_the_game) {
    Wizard w = runPxn();
    std::string text = buildProfile("PXN-V10 \"Wheel\"", 0x11ff, 0x3245, w.steps(), w.results(), "2026-09-21 12:00");
    joystick::Profile p; std::string err;
    CHECK(parseText(text, p, err));
    if (!err.empty()) std::fprintf(stderr, "%s\n%s\n", err.c_str(), text.c_str());
    CHECK_EQ(p.name, std::string("PXN-V10 \"Wheel\""));
    CHECK_EQ(p.axes.size(), (size_t)4);
    CHECK(p.axes[0].invert);                                                     // wheel right (+) -> yaw - (yaw + = turn left), like the shipped profile
    CHECK_EQ(p.buttons.size(), (size_t)7);                                       // weapon_prev has no action; lock steps skipped
    CHECK(text.find("weapon_prev") != std::string::npos);                        // noted in the header as skipped
    // a simulated PXN through the game's own mapper: wheel right, throttle full, brake half, clutch full, fire and dock held
    joystick::RawState s; s.axes.assign(8, 0); s.buttons.assign(16, 0); s.hats.assign(1, 0);
    s.axes[0] = 32767; s.axes[2] = 32767; s.axes[5] = 0; s.axes[6] = 32767; s.buttons[0] = 1; s.buttons[13] = 1;
    joystick::Mapper m; std::vector<joystick::Contribution> out;
    m.evaluate(p, s, {}, out);
    CHECK(near(sum(out, "yaw"), -1.0));                                          // turn right
    CHECK(near(sum(out, "thrust"), 1.0));
    CHECK(near(sum(out, "brake"), 0.5, 0.03));
    CHECK(near(sum(out, "lift"), -1.0));                                         // clutch -> lift down
    CHECK(near(sum(out, "fire"), 1.0) && near(sum(out, "dock"), 1.0));
    // pedals at rest and the wheel centred: nothing moves
    s.axes.assign(8, 0); for (int a : {2, 5, 6}) s.axes[(size_t)a] = -32768; s.buttons.assign(16, 0);
    out.clear(); m.evaluate(p, s, {}, out);
    CHECK(near(sum(out, "yaw"), 0) && near(sum(out, "thrust"), 0) && near(sum(out, "brake"), 0) && near(sum(out, "lift"), 0));
    // without the clutch: three axes, still valid
    Wizard w2 = runPxn(false);
    joystick::Profile p2;
    CHECK(parseText(buildProfile("x", 1, 2, w2.steps(), w2.results(), "d"), p2, err));
    CHECK_EQ(p2.axes.size(), (size_t)3);
}

TEST(controller_setup_slug_and_file_name) {
    CHECK_EQ(slug("PXN-V10 Wheel!"), std::string("pxn_v10_wheel"));
    CHECK_EQ(slug("  ***  "), std::string("device"));
    CHECK_EQ(slug("Microsoft SideWinder Joystick With A Very Long Name Indeed"), std::string("microsoft_sidewinder_joystick_wi"));
    CHECK(slug("Microsoft SideWinder Joystick With A Very Long Name Indeed").size() <= 32);
    CHECK_EQ(profileFileName(0x11ff, 0x3245, "PXN-V10"), std::string("11ff_3245_pxn_v10.json"));
    CHECK_EQ(profileFileName(0x45e, 0x3c, "SideWinder"), std::string("045e_003c_sidewinder.json"));
}

TEST(controller_setup_layout_fits_every_window) {
    const float sizes[][2] = {{1280, 720}, {1920, 1080}, {1280, 1024}, {800, 600}, {640, 480}, {3840, 2160}, {2560, 1080}};
    for (auto& s : sizes) {
        float sc = std::min(s[0] / 1280.0f, s[1] / 720.0f); sc = std::clamp(sc, 0.6f, 3.0f);   // core::uiLayoutScale
        Layout L = computeLayout(s[0], s[1], sc);
        Rect screen{0, 0, s[0], s[1]};
        CHECK(L.panel.inside(screen));
        CHECK(L.tabs.inside(L.panel) && L.body.inside(L.panel) && L.footer.inside(L.panel));
        CHECK(L.body.h > 200 * sc);                                              // room for the prompt, the buttons and the step list
        CHECK(L.panel.w >= 0.6f * s[0] - 1);
    }
    Layout L = computeLayout(1280, 720, 1.0f);
    CHECK(near(L.panel.w, 1024, 1) && near(L.panel.x, 128, 1));                  // ~80% at the reference size
}
