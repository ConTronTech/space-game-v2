#include <filesystem>
#include <fstream>
#include <sstream>
#include "core/input_handler/input_handler.h"
#include "core/input_methods/joystick/joystick_profile.h"
#include "core/input_methods/joystick/joystick_rules.h"
#include "engine/engine.h"
#include "tests/test.h"

namespace {
using namespace joystick;
bool close(double a, double b, double tol = 1e-4) { return std::fabs(a - b) <= tol; }
Profile parse(const char* json, bool* ok = nullptr, std::string* err = nullptr) {
    std::string e; Profile p;
    engine::Json j = engine::Json::parse(json, &e);
    bool good = parseProfile(j, p, e);
    if (ok) *ok = good;
    if (err) *err = e;
    return p;
}
float actionValue(const std::vector<Contribution>& c, const std::string& a) { float sum = 0; for (auto& x : c) if (x.action == a) sum += x.value; return sum; }
} // namespace

TEST(joystick_bipolar_normalisation_uses_the_calibrated_centre) {
    Calibration c;                                                        // default: -32768 .. 32767, centre 0
    CHECK(close(normalizeBipolar(0, c), 0.0)); CHECK(close(normalizeBipolar(32767, c), 1.0)); CHECK(close(normalizeBipolar(-32768, c), -1.0));
    CHECK(close(normalizeBipolar(16384, c), 0.5, 1e-3));
    CHECK(close(normalizeBipolar(99999, c), 1.0)); CHECK(close(normalizeBipolar(-99999, c), -1.0));   // beyond the stops: clamped
    Calibration off{-30000, 20000, 1000};                                 // an off-centre stick: each half is scaled to its own stop
    CHECK(close(normalizeBipolar(1000, off), 0.0)); CHECK(close(normalizeBipolar(20000, off), 1.0)); CHECK(close(normalizeBipolar(-30000, off), -1.0));
    CHECK(close(normalizeBipolar(10500, off), 0.5, 1e-3)); CHECK(close(normalizeBipolar(-14500, off), -0.5, 1e-3));
    Calibration broken{100, 100, 100};                                    // zero span: no division by zero
    CHECK(close(normalizeBipolar(50, broken), 0.0)); CHECK(close(normalizeBipolar(150, broken), 0.0));
}

TEST(joystick_deadzone_rescales_and_is_continuous) {
    CHECK(close(applyDeadzone(0.0f, 0.1f), 0.0)); CHECK(close(applyDeadzone(0.1f, 0.1f), 0.0)); CHECK(close(applyDeadzone(-0.05f, 0.1f), 0.0));
    CHECK(close(applyDeadzone(1.0f, 0.1f), 1.0)); CHECK(close(applyDeadzone(-1.0f, 0.1f), -1.0));     // still reaches full travel
    CHECK(close(applyDeadzone(0.55f, 0.1f), 0.5)); CHECK(close(applyDeadzone(-0.55f, 0.1f), -0.5));    // (0.55-0.1)/0.9
    CHECK(applyDeadzone(0.1001f, 0.1f) < 0.001f);                          // continuous at the edge: no jump
    float prev = -1; for (float v = 0; v <= 1.0f; v += 0.01f) { float o = applyDeadzone(v, 0.2f); CHECK(o >= prev - 1e-6f); prev = o; }   // monotonic
    CHECK(close(applyDeadzone(0.5f, 0.0f), 0.5));                          // no dead zone: unchanged
    CHECK(std::fabs(applyDeadzone(0.999f, 5.0f)) <= 1.0f);                 // absurd dead zone is clamped, no NaN
}

TEST(joystick_curve_and_invert) {
    CHECK(close(applyCurve(0.5f, 1.0f), 0.5)); CHECK(close(applyCurve(0.5f, 2.0f), 0.25)); CHECK(close(applyCurve(-0.5f, 2.0f), -0.25));   // sign kept
    CHECK(close(applyCurve(1.0f, 3.0f), 1.0)); CHECK(close(applyCurve(0.0f, 2.0f), 0.0)); CHECK(close(applyCurve(0.5f, 0.0f), 0.5));
    CHECK(applyCurve(0.5f, 0.5f) > 0.5f);                                  // < 1: more sensitive near the centre
    AxisMap a; a.action = "yaw"; a.invert = true;
    CHECK(close(axisValue(a, 32767, 1.0f), -1.0)); CHECK(close(axisValue(a, -32768, 1.0f), 1.0));
    a.scale = 0.5f; CHECK(close(axisValue(a, 32767, 1.0f), -0.5));         // scale multiplies
    a.invert = false; a.scale = -1.0f; CHECK(close(axisValue(a, 32767, 1.0f), -1.0));   // a negative scale is the same as invert
}

TEST(joystick_pedals_split_rest_at_min_or_max) {
    Calibration c;
    CHECK(close(normalizePedal(-32768, c, Rest::Min), 0.0)); CHECK(close(normalizePedal(32767, c, Rest::Min), 1.0));     // rests at min, pressed to max
    CHECK(close(normalizePedal(32767, c, Rest::Max), 0.0)); CHECK(close(normalizePedal(-32768, c, Rest::Max), 1.0));     // rests at max, pressed to min
    CHECK(close(normalizePedal(0, c, Rest::Min), 0.5, 1e-3));
    Calibration narrow{-20000, 15000, 0};                                  // a pedal with its own travel: unpressed reads 0, fully pressed 1
    CHECK(close(normalizePedal(-20000, narrow, Rest::Min), 0.0)); CHECK(close(normalizePedal(15000, narrow, Rest::Min), 1.0));
    CHECK(close(normalizePedal(-99999, narrow, Rest::Min), 0.0)); CHECK(close(normalizePedal(99999, narrow, Rest::Min), 1.0));   // clamped
    Calibration bad{5, 5, 5}; CHECK(close(normalizePedal(5, bad, Rest::Max), 0.0));
    AxisMap thr; thr.role = Role::Throttle; thr.action = "thrust"; thr.rest = Rest::Max; thr.deadzone = 0.05f;
    CHECK(close(axisValue(thr, 32767, 1.0f), 0.0));                        // not pressed: exactly 0 (inside the dead zone)
    CHECK(close(axisValue(thr, -32768, 1.0f), 1.0));                       // fully pressed: 1
}

TEST(joystick_combined_pedals_split_one_axis_into_throttle_and_brake) {
    Profile p = parse(R"({"axes":[{"index":2,"role":"combined_pedals","above":"thrust","below":"brake","deadzone":0.0}]})");
    Mapper m; std::vector<Contribution> out; Tuning t;
    RawState s; s.axes = {0, 0, 32767};
    m.evaluate(p, s, t, out); CHECK(close(actionValue(out, "thrust"), 1.0)); CHECK(close(actionValue(out, "brake"), 0.0));   // full throttle
    out.clear(); s.axes[2] = -32768;
    m.evaluate(p, s, t, out); CHECK(close(actionValue(out, "brake"), 1.0)); CHECK(close(actionValue(out, "thrust"), 0.0));   // full brake
    out.clear(); s.axes[2] = 16384;
    m.evaluate(p, s, t, out); CHECK(close(actionValue(out, "thrust"), 0.5, 1e-3));
    out.clear(); s.axes[2] = 0;
    m.evaluate(p, s, t, out); CHECK(out.empty());                          // both pedals up: nothing
    Profile withDz = parse(R"({"axes":[{"index":0,"role":"combined_pedals","deadzone":0.1}]})");   // default action names: thrust above, brake below
    out.clear(); RawState r; r.axes = {-32768}; m.evaluate(withDz, r, t, out); CHECK(close(actionValue(out, "brake"), 1.0));
}

TEST(joystick_buttons_hold_and_toggle) {
    Profile p = parse(R"({"buttons":[{"index":0,"action":"fire"},{"index":1,"action":"toggle_warp","mode":"toggle"},{"index":2,"action":"dock","scale":0.5}]})");
    Mapper m; Tuning t; RawState s; s.buttons = {0, 0, 0};
    std::vector<Contribution> out;
    m.evaluate(p, s, t, out); CHECK(out.empty());
    s.buttons[0] = 1; m.evaluate(p, s, t, out); CHECK(close(actionValue(out, "fire"), 1.0));                 // hold: active while pressed
    out.clear(); s.buttons[0] = 0; m.evaluate(p, s, t, out); CHECK(out.empty());
    // toggle: on after the first press, stays on when released, off after the second press
    s.buttons[1] = 1; out.clear(); m.evaluate(p, s, t, out); CHECK(close(actionValue(out, "toggle_warp"), 1.0));
    s.buttons[1] = 1; out.clear(); m.evaluate(p, s, t, out); CHECK(close(actionValue(out, "toggle_warp"), 1.0));   // still held: no second flip
    s.buttons[1] = 0; out.clear(); m.evaluate(p, s, t, out); CHECK(close(actionValue(out, "toggle_warp"), 1.0));   // released: stays on
    s.buttons[1] = 1; out.clear(); m.evaluate(p, s, t, out); CHECK(out.empty());                                    // second press: off
    s.buttons[1] = 0; s.buttons[2] = 1; out.clear(); m.evaluate(p, s, t, out); CHECK(close(actionValue(out, "dock"), 0.5));   // button scale
    s.buttons.clear(); out.clear(); m.evaluate(p, s, t, out); CHECK(out.empty());                                   // a button the device does not have reads as up
}

TEST(joystick_hat_becomes_four_buttons) {
    Profile p = parse(R"({"hats":[{"index":0,"up":"weapon_1","down":"weapon_2","left":"camera_next","right":"dock"}]})");
    Mapper m; Tuning t; RawState s; s.hats = {0};
    std::vector<Contribution> out;
    m.evaluate(p, s, t, out); CHECK(out.empty());
    s.hats[0] = kHatUp; m.evaluate(p, s, t, out); CHECK(close(actionValue(out, "weapon_1"), 1.0)); CHECK_EQ(out.size(), (size_t)1);
    out.clear(); s.hats[0] = kHatDown | kHatRight; m.evaluate(p, s, t, out);      // a diagonal presses two
    CHECK(close(actionValue(out, "weapon_2"), 1.0)); CHECK(close(actionValue(out, "dock"), 1.0)); CHECK_EQ(out.size(), (size_t)2);
    out.clear(); s.hats[0] = kHatLeft; m.evaluate(p, s, t, out); CHECK(close(actionValue(out, "camera_next"), 1.0));
    out.clear(); RawState none; m.evaluate(p, none, t, out); CHECK(out.empty());   // no hat on the device
}

TEST(joystick_button_groups_give_discrete_values_and_a_default) {
    Profile p = parse(R"({"groups":[{"action":"throttle_limit","buttons":{"12":0.2,"13":0.5,"14":1.0},"default":0.0}]})");
    Mapper m; Tuning t; RawState s; s.buttons.assign(20, 0);
    std::vector<Contribution> out;
    m.evaluate(p, s, t, out); CHECK(close(actionValue(out, "throttle_limit"), 0.0)); CHECK_EQ(out.size(), (size_t)1);   // neutral: the default
    s.buttons[13] = 1; out.clear(); m.evaluate(p, s, t, out); CHECK(close(actionValue(out, "throttle_limit"), 0.5));
    s.buttons[14] = 1; out.clear(); m.evaluate(p, s, t, out); CHECK(close(actionValue(out, "throttle_limit"), 1.0)); CHECK_EQ(out.size(), (size_t)1);   // two gears at once: the highest wins
    Profile noDefault = parse(R"({"groups":[{"action":"reverse","buttons":{"18":1}}]})");
    RawState r; r.buttons.assign(20, 0); out.clear(); m.evaluate(noDefault, r, t, out); CHECK(out.empty());              // no default: nothing
    r.buttons[18] = 1; m.evaluate(noDefault, r, t, out); CHECK(close(actionValue(out, "reverse"), 1.0));                  // reverse gear -> a flag
}

TEST(joystick_user_tuning_scales_deadzone_and_sensitivity) {
    Profile p = parse(R"({"axes":[{"index":0,"role":"stick","action":"roll","deadzone":0.2},{"index":1,"role":"throttle","action":"thrust","rest":"min"}]})");
    Mapper m; RawState s; s.axes = {8000, 32767};                          // 0.244 of travel
    std::vector<Contribution> out; Tuning t;
    m.evaluate(p, s, t, out); float base = actionValue(out, "roll"); CHECK(base > 0.0f && base < 0.1f);
    out.clear(); t.deadzoneScale = 2.0f; m.evaluate(p, s, t, out); CHECK(close(actionValue(out, "roll"), 0.0));   // bigger dead zone swallows it (0.4 > 0.244)
    out.clear(); t.deadzoneScale = 0.5f; m.evaluate(p, s, t, out); CHECK(actionValue(out, "roll") > base);
    out.clear(); t.deadzoneScale = 1.0f; t.sensitivity = 2.0f; m.evaluate(p, s, t, out);
    CHECK(close(actionValue(out, "roll"), base * 2.0)); CHECK(close(actionValue(out, "thrust"), 1.0));                 // sensitivity: sticks yes, pedals no
    out.clear(); t.deadzoneScale = 100.0f; m.evaluate(p, s, t, out); CHECK(std::isfinite(actionValue(out, "roll")));     // absurd values are clamped
}

TEST(joystick_profile_matching_by_ids_then_name_then_fallback) {
    std::vector<Profile> ps;
    ps.push_back(parse(R"({"name":"wheel","match":{"vid":"11ff","pid":"3245","name_contains":"PXN"}})"));
    ps.push_back(parse(R"({"name":"stick","match":{"name_contains":"sidewinder"}})"));
    ps.push_back(parse(R"({"name":"other","match":{"vid":"1234","pid":"5678"}})"));
    CHECK_EQ(matchProfile(ps, 0x11ff, 0x3245, "whatever"), 0);                   // ids win, whatever the name says
    CHECK_EQ(matchProfile(ps, 0x0000, 0x0000, "PXN-V10 wheel"), 0);              // name substring, case-insensitive
    CHECK_EQ(matchProfile(ps, 0x045e, 0x003c, "Microsoft SideWinder Joystick"), 1);
    CHECK_EQ(matchProfile(ps, 0x1234, 0x5678, "no name"), 2);
    CHECK_EQ(matchProfile(ps, 0x9999, 0x9999, "Mystery Gamepad"), -1);           // unknown: the caller uses the generic fallback (maps nothing)
    CHECK_EQ(matchProfile(ps, 0x11ff, 0x9999, "PXN"), 0);                        // the vid alone is not enough, but the name still matches
    CHECK_EQ(matchProfile({}, 1, 2, "x"), -1);
    CHECK_EQ(matchProfile(ps, 0x1234, 0x0001, ""), -1);                          // half of the ids (or an empty name) matches nothing
}

TEST(joystick_profile_parsing_and_errors) {
    bool ok = false; std::string err;
    Profile p = parse(R"({"name":"t","status":"UNVERIFIED","match":{"vid":"11ff","pid":"3245"},
        "axes":[{"index":1,"role":"steering","action":"yaw","invert":true,"deadzone":0.05,"curve":1.5,"scale":2,"calibration":{"min":-1000,"max":1000,"centre":10}},
                {"index":3,"role":"throttle","action":"thrust","rest":"max"}],
        "buttons":[{"index":4,"action":"fire","mode":"toggle"}]})", &ok, &err);
    CHECK(ok); CHECK_EQ(p.name, std::string("t")); CHECK_EQ(p.match.vid, 0x11ff); CHECK_EQ(p.match.pid, 0x3245);
    CHECK_EQ(p.axes.size(), (size_t)2); CHECK(p.axes[0].invert && close(p.axes[0].scale, 2.0) && close(p.axes[0].curve, 1.5));
    CHECK_EQ(p.axes[0].cal.min, -1000); CHECK_EQ(p.axes[0].cal.centre, 10); CHECK(p.axes[1].rest == Rest::Max); CHECK(p.axes[1].role == Role::Throttle);
    CHECK(p.buttons[0].mode == ButtonMode::Toggle);
    // every kind of broken file is reported, never a crash
    for (const char* bad : {"[]", R"({"axes":[{"role":"stick","action":"yaw"}]})", R"({"axes":[{"index":0,"role":"warpdrive","action":"yaw"}]})",
                            R"({"axes":[{"index":0,"role":"stick"}]})", R"({"axes":[{"index":0,"action":"yaw","calibration":{"min":5,"max":5}}]})",
                            R"({"axes":[{"index":0,"action":"yaw","rest":"middle"}]})", R"({"buttons":[{"index":1}]})", R"({"buttons":[{"index":1,"action":"a","mode":"sideways"}]})",
                            R"({"match":{"vid":"zzzz"}})", R"({"groups":[{"buttons":{"1":1}}]})", R"({"axes":[{"index":99,"action":"yaw"}]})"}) {
        Profile q; std::string e; engine::Json j = engine::Json::parse(bad, &e);
        bool good = parseProfile(j, q, e);
        CHECK(!good); CHECK(!e.empty());
    }
    engine::Json empty = engine::Json::parse("{}", &err); Profile e2; CHECK(parseProfile(empty, e2, err)); CHECK(e2.empty());   // an empty object is a valid profile that maps nothing
}

TEST(joystick_shipped_profiles_parse_and_state_their_verification_status) {
    int n = 0;
    for (auto& e : std::filesystem::directory_iterator("config/input/devices")) {                  // tests run from the repo root
        if (e.path().extension() != ".json") continue;
        std::ifstream f(e.path()); std::stringstream ss; ss << f.rdbuf();
        std::string err; engine::Json j = engine::Json::parse(ss.str(), &err);
        Profile p; bool ok = parseProfile(j, p, err);
        CHECK(ok); if (!ok) std::fprintf(stderr, "  %s: %s\n", e.path().c_str(), err.c_str());
        CHECK(p.status.find("UNVERIFIED") != std::string::npos || p.status.find("VERIFIED") != std::string::npos);   // every shipped profile states VERIFIED (guided run on the real device) or UNVERIFIED (a guess)
        CHECK(!p.empty()); CHECK(p.match.vid >= 0);
        for (auto& a : p.axes) CHECK(a.cal.max > a.cal.min);
        n++;
    }
    CHECK(n >= 2);
    // the two named devices resolve to their own profiles
    std::vector<Profile> ps;
    for (auto& e : std::filesystem::directory_iterator("config/input/devices")) { if (e.path().extension() != ".json") continue; std::ifstream f(e.path()); std::stringstream ss; ss << f.rdbuf(); std::string err; Profile p; if (parseProfile(engine::Json::parse(ss.str(), &err), p, err)) ps.push_back(p); }
    int wheel = matchProfile(ps, 0x11ff, 0x3245, "PXN-V10"), stick = matchProfile(ps, 0x045e, 0x003c, "Microsoft SideWinder Joystick");
    CHECK(wheel >= 0 && stick >= 0 && wheel != stick);
}

TEST(joystick_hotplug_table_add_remove_and_slot_reuse) {
    DeviceTable t;
    CHECK_EQ(t.add(10), 0); CHECK_EQ(t.add(11), 1); CHECK_EQ(t.count(), 2);
    CHECK_EQ(t.add(10), -1); CHECK_EQ(t.count(), 2);                       // announced twice (present at start AND reported by an event): ignored
    CHECK_EQ(t.remove(10), 0); CHECK_EQ(t.count(), 1); CHECK_EQ(t.slotOf(10), -1); CHECK_EQ(t.slotOf(11), 1);
    CHECK_EQ(t.remove(10), -1); CHECK_EQ(t.remove(999), -1);               // removing what is not there: ignored
    CHECK_EQ(t.add(12), 0);                                                 // the freed slot is reused (lowest first)
    CHECK_EQ(t.add(13), 2); CHECK_EQ(t.slots(), 3);
    t.remove(11); t.remove(12); t.remove(13); CHECK_EQ(t.count(), 0);
    CHECK_EQ(t.add(14), 0);                                                 // a re-plugged device (new instance id) gets the first slot again
}

TEST(joystick_calibration_tracker_finds_ranges_and_suggests_roles) {
    CalibrationTracker c;
    // 0: wheel (centred, moves both ways), 1: pedal resting at max going to min, 2: pedal resting at min, 3: untouched
    int steer[] = {0, -32000, 30000, 100, -200}, ped1[] = {32767, 20000, -30000, 0, 32000}, ped2[] = {-32768, -20000, 10000, 32000, -32768}, unused[] = {5, 5, 6, 5, 4};
    for (int i = 0; i < 5; i++) c.update({steer[i], ped1[i], ped2[i], unused[i]});
    const auto& r = c.ranges();
    CHECK_EQ(r.size(), (size_t)4);
    CHECK_EQ(r[0].min, -32000); CHECK_EQ(r[0].max, 30000); CHECK_EQ(r[0].first, 0);
    CHECK_EQ(std::string(CalibrationTracker::suggestRole(r[0])), std::string("stick / steering (centred)"));
    CHECK_EQ(std::string(CalibrationTracker::suggestRole(r[1])), std::string("pedal resting at max"));
    CHECK_EQ(std::string(CalibrationTracker::suggestRole(r[2])), std::string("pedal resting at min"));
    CHECK(std::string(CalibrationTracker::suggestRole(r[3])).find("unused") == 0);
    CalibrationTracker none; CHECK(none.ranges().empty());
    none.update({}); CHECK(none.ranges().empty());
}

// ---- end to end: a fake device + a profile -> the real IInput's action values ----
namespace {
struct FakePad : core::InputMethod {
    Profile profile; RawState state; Mapper mapper; Tuning tuning; std::vector<Contribution> out;
    const char* device() const override { return "joystick"; }
    bool addBinding(const std::string&, const engine::Json&, std::string&) override { return false; }
    void clearBindings() override {}
    void poll(core::IInput& in) override {                                  // the same three lines the module's poll() runs
        out.clear();
        mapper.evaluate(profile, state, tuning, out);
        for (auto& c : out) in.contribute(c.action, c.value);
    }
};
} // namespace

TEST(joystick_end_to_end_fake_stick_drives_pitch_yaw_roll_and_thrust) {
    engine::Engine eng;
    core::InputHandler in;
    in.init(eng);
    FakePad pad;
    pad.profile = parse(R"({"axes":[
        {"index":0,"role":"stick","action":"roll"},
        {"index":1,"role":"stick","action":"pitch"},
        {"index":2,"role":"stick","action":"yaw","invert":true},
        {"index":3,"role":"throttle","action":"thrust","rest":"min"}],
      "buttons":[{"index":0,"action":"fire"}]})");
    pad.state.axes = {0, 0, 0, -32768}; pad.state.buttons = {0};
    in.registerMethod(&pad);
    in.onFrameBegin(eng);                                                    // first frame: idle
    CHECK(close(in.value("pitch"), 0.0)); CHECK(close(in.value("thrust"), 0.0)); CHECK(!in.down("fire"));
    pad.state.axes = {16384, 32767, 32767, 32767}; pad.state.buttons = {1};
    in.onFrameBegin(eng);
    CHECK(close(in.value("roll"), 0.5, 1e-3));                               // stick half right
    CHECK(close(in.value("pitch"), 1.0));                                    // pulled fully back
    CHECK(close(in.value("yaw"), -1.0));                                     // twisted right: yaw is + = left, so -1
    CHECK(close(in.value("thrust"), 1.0));                                   // throttle fully pressed
    CHECK(in.down("fire")); CHECK(in.pressed("fire"));                       // a joystick button is a fresh press exactly like a key
    in.onFrameBegin(eng); CHECK(in.down("fire")); CHECK(!in.pressed("fire"));   // held: no new press
    pad.state.buttons = {0}; in.onFrameBegin(eng); CHECK(!in.down("fire")); CHECK(in.released("fire"));
    // the same action from two sources adds up (keyboard-like contribution) and is clamped by IInput
    pad.state.axes[1] = 16384; in.onFrameBegin(eng);                          // the stick alone: 0.5
    CHECK(close(in.value("pitch"), 0.5, 1e-3));
    in.contribute("pitch", 0.25f);                                           // a key press on top (what the keyboard method does) adds
    CHECK(close(in.value("pitch"), 0.75, 1e-3));
    in.unregisterMethod(&pad);
}

TEST(joystick_end_to_end_unknown_device_maps_nothing_and_a_bad_profile_falls_back) {
    engine::Engine eng;
    core::InputHandler in;
    in.init(eng);
    FakePad pad;                                                             // the generic fallback: an empty profile
    pad.state.axes = {32767, -32768, 32767, 32767}; pad.state.buttons = {1, 1, 1};
    in.registerMethod(&pad);
    in.onFrameBegin(eng);
    for (const char* a : {"roll", "pitch", "yaw", "thrust", "brake", "fire", "lift"}) CHECK(close(in.value(a), 0.0));   // nothing can make the ship spin
    bool ok = true; std::string err;
    Profile bad = parse(R"({"axes":[{"index":0,"role":"nonsense","action":"yaw"}]})", &ok, &err);
    CHECK(!ok); CHECK(bad.empty());                                          // a failed parse leaves an empty profile: the fallback
    pad.profile = bad; in.onFrameBegin(eng); CHECK(close(in.value("yaw"), 0.0));
    in.unregisterMethod(&pad);
}
