#include <set>
#include "tests/test.h"
#include "ui/main_menu/main_menu_rules.h"

using namespace mainmenu;

TEST(main_menu_should_show_precedence) {
    CHECK(shouldShow(true, false, false, false));           // normal launch
    CHECK(!shouldShow(false, false, false, false));         // main_menu.enabled = false
    CHECK(!shouldShow(true, false, false, true));           // automated run (--frames, --benchmark, ...)
    CHECK(shouldShow(true, true, false, true));             // --main-menu beats an automation flag
    CHECK(shouldShow(false, true, false, false));           // ... and the config switch
    CHECK(!shouldShow(true, true, true, false));            // --no-main-menu beats everything
}

TEST(main_menu_automation_flags_detected) {
    CHECK(!anyAutomationFlag([](const std::string&) { return false; }));
    for (const char* f : {"frames", "benchmark", "paused", "ui-click", "auto-fire"}) {
        std::string want = f;
        CHECK(anyAutomationFlag([&](const std::string& g) { return g == want; }));
    }
    // flags that only set up the world for a human (display, settings file, ...) do not skip the menu
    std::set<std::string> human = {"display", "fullscreen", "settings", "saves", "quality", "no-vsync", "main-menu"};
    CHECK(!anyAutomationFlag([&](const std::string& g) { return human.count(g) > 0; }));
}

TEST(main_menu_items_by_available_services) {
    auto full = buildItems(true, true, true);
    CHECK_EQ(full.size(), (size_t)5);
    CHECK(full.front() == Item::Continue);                  // first = default focus when there is something to continue
    CHECK(full.back() == Item::Quit);

    auto noSaves = buildItems(true, false, true);           // save system present, nothing on disk yet
    CHECK_EQ(noSaves.size(), (size_t)4);
    CHECK(noSaves.front() == Item::NewGame);
    CHECK(noSaves[1] == Item::Load);                        // still listed: the page says "No saved games yet"

    auto bare = buildItems(false, true, false);             // no save system, no pause menu to host Settings
    CHECK_EQ(bare.size(), (size_t)2);
    CHECK(bare[0] == Item::NewGame);
    CHECK(bare[1] == Item::Quit);
}

TEST(main_menu_continue_slot_choice) {
    std::vector<core::SlotInfo> slots = {{"autosave", 300}, {"save_b", 200}, {"save_a", 100}};   // newest first
    CHECK_EQ(continueSlot("", slots), std::string("autosave"));           // fresh process: newest on disk, autosave included
    CHECK_EQ(continueSlot("save_a", slots), std::string("save_a"));       // an active slot that still exists wins
    CHECK_EQ(continueSlot("deleted", slots), std::string("autosave"));    // stale active slot: fall back to newest
    CHECK_EQ(continueSlot("", {}), std::string());                        // nothing to continue
    CHECK_EQ(continueSlot("save_a", {}), std::string());
}

TEST(main_menu_focus_wraps) {
    CHECK_EQ(moveFocus(0, -1, 5), 4);
    CHECK_EQ(moveFocus(4, +1, 5), 0);
    CHECK_EQ(moveFocus(2, +1, 5), 3);
    CHECK_EQ(moveFocus(3, 1, 0), 0);                        // empty list: no division by zero
}

TEST(main_menu_slot_label_marks_autosave) {
    std::string a = slotLabel({"autosave", 1758500000});
    std::string m = slotLabel({"save_20250922_010000", 1758500000});
    CHECK(a.rfind("Autosave  ", 0) == 0);
    CHECK(m.rfind("Autosave", 0) != 0);
    CHECK(a.size() > 10 && m.size() > 10);                  // the date is there (exact text depends on the local time zone)
    CHECK_EQ(label(Item::NewGame), std::string("New Game"));
}
