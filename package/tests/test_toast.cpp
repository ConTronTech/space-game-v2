// Tests for ui/toast's logic (toast_rules.h): stacking order, cap + queue overflow, expiry, fade curve, level colours, layout.
#include <cmath>
#include <string>
#include "tests/test.h"
#include "ui/toast/toast_rules.h"

using namespace toast;

namespace {
bool near(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol; }
}

TEST(toast_stacking_order_oldest_first) {
    Stack s(4);
    s.add(Level::Info, "a", 4, 0.0);
    s.add(Level::Warning, "b", 4, 0.1);
    s.add(Level::Urgent, "c", 4, 0.2);
    CHECK_EQ(s.visible(), 3);
    CHECK_EQ(s.at(0).text, std::string("a"));
    CHECK_EQ(s.at(2).text, std::string("c"));
    CHECK(s.at(2).level == Level::Urgent);
    // layout: the newest (last) is lowest on screen, older ones above it
    float yNew = slotY(2, 3, 720, 16, 30, 6), yOld = slotY(0, 3, 720, 16, 30, 6);
    CHECK(near(yNew, 720 - 16 - 30));
    CHECK(near(yOld, yNew - 2 * 36));
    CHECK(near(slotX(200, 1280, 16), 1064));
}

TEST(toast_cap_queues_extra_and_promotes_on_expiry) {
    Stack s(2);
    s.add(Level::Info, "a", 1, 0.0);
    s.add(Level::Info, "b", 3, 0.0);
    s.add(Level::Info, "c", 1, 0.5);
    CHECK_EQ(s.visible(), 2);
    CHECK_EQ(s.waiting(), 1);
    CHECK(s.at(2).shownAt < 0);           // waiting: not aging
    CHECK(near(s.alpha(2, 0.5), 0));
    s.update(1.0);                        // "a" expires, "c" appears now
    CHECK_EQ(s.visible(), 2);
    CHECK_EQ(s.waiting(), 0);
    CHECK_EQ(s.at(0).text, std::string("b"));
    CHECK_EQ(s.at(1).text, std::string("c"));
    CHECK(near((float)s.at(1).shownAt, 1.0f));
    s.update(1.9);                        // c shown at 1.0 with 1 s life: still there
    CHECK_EQ(s.size(), 2);
    s.update(2.0);
    CHECK_EQ(s.size(), 1);
    s.update(3.0);
    CHECK_EQ(s.size(), 0);
}

TEST(toast_expiry_of_middle_entry_keeps_order) {
    Stack s(4);
    s.add(Level::Info, "a", 5, 0);
    s.add(Level::Info, "b", 1, 0);
    s.add(Level::Info, "c", 5, 0);
    s.update(1.5);
    CHECK_EQ(s.size(), 2);
    CHECK_EQ(s.at(0).text, std::string("a"));
    CHECK_EQ(s.at(1).text, std::string("c"));
}

TEST(toast_queue_overflow_drops_oldest_waiting) {
    Stack s(2);
    for (int i = 0; i < Stack::kCapacity; i++) s.add(Level::Info, std::to_string(i), 4, 0);
    CHECK_EQ(s.size(), Stack::kCapacity);
    CHECK_EQ((int)s.dropped(), 0);
    s.add(Level::Urgent, "new", 4, 0);
    CHECK_EQ(s.size(), Stack::kCapacity);
    CHECK_EQ((int)s.dropped(), 1);
    CHECK_EQ(s.at(0).text, std::string("0"));     // visible ones untouched
    CHECK_EQ(s.at(1).text, std::string("1"));
    CHECK_EQ(s.at(2).text, std::string("3"));     // "2" (oldest waiting) dropped
    CHECK_EQ(s.at(Stack::kCapacity - 1).text, std::string("new"));
    // every slot visible: the oldest visible goes
    Stack all(Stack::kCapacity);
    for (int i = 0; i <= Stack::kCapacity; i++) all.add(Level::Info, std::to_string(i), 4, 0);
    CHECK_EQ(all.at(0).text, std::string("1"));
    all.clear();
    CHECK_EQ(all.size(), 0);
}

TEST(toast_fade_curve_linear_last_half_second) {
    CHECK(near(fadeAlpha(0, 4), 1));
    CHECK(near(fadeAlpha(3.5, 4), 1));
    CHECK(near(fadeAlpha(3.75, 4), 0.5f));
    CHECK(near(fadeAlpha(3.9, 4), 0.2f, 1e-3f));
    CHECK(near(fadeAlpha(4.0, 4), 0));
    CHECK(near(fadeAlpha(10, 4), 0));
    CHECK(near(fadeAlpha(0.15, 0.3f), 0.5f));    // shorter than the fade: fades over its whole life
}

TEST(toast_level_colours_and_flash) {
    RGB i = levelColor(Level::Info), w = levelColor(Level::Warning), u = levelColor(Level::Urgent);
    CHECK(near(i.r, 0.45f) && near(i.g, 0.85f) && near(i.b, 1.0f));
    CHECK(near(w.r, 1.0f) && near(w.g, 0.75f) && near(w.b, 0.20f));
    CHECK(near(u.r, 1.0f) && near(u.g, 0.25f) && near(u.b, 0.22f));
    CHECK(near(flash(Level::Info, 1.23), 1) && near(flash(Level::Warning, 1.23), 1));
    float lo = 2, hi = -1;
    for (int k = 0; k < 1000; k++) { float f = flash(Level::Urgent, k * 0.001); lo = std::min(lo, f); hi = std::max(hi, f); }
    CHECK(lo >= 0.549f && lo < 0.6f);
    CHECK(hi <= 1.001f && hi > 0.95f);
}
