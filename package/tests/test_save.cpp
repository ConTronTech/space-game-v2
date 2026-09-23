#include <filesystem>
#include <fstream>
#include "core/save_system/save_system.h"
#include "engine/engine.h"
#include "tests/test.h"

namespace fs = std::filesystem;

namespace {
struct Thing : core::ISaveable {
    std::string id; int hp = 100; float x = 0;
    explicit Thing(std::string i) : id(std::move(i)) {}
    const char* saveId() const override { return id.c_str(); }
    engine::Json save() const override { return engine::Json::object().set("hp", hp).set("x", x); }
    void load(const engine::Json& j) override { hp = (int)j["hp"].num(hp); x = (float)j["x"].num(x); }
};

struct Rig {
    std::string dir;
    engine::Engine eng;
    core::SaveSystem sys;
    long long t = 1000;
    explicit Rig(const char* name) : dir((fs::temp_directory_path() / (std::string("sgv2_save_") + name)).string()) {
        fs::remove_all(dir);
        fs::create_directories(dir);   // tests write raw files into it
        sys.setDir(dir);
        sys.setClock([this] { return t; });
        sys.init(eng);
    }
    ~Rig() { sys.shutdown(eng); fs::remove_all(dir); }
};
} // namespace

TEST(save_roundtrip_restores_each_module) {
    Rig r("rt");
    Thing a("ship/core"), b("gameplay/inventory");
    r.sys.registerSaveable(&a); r.sys.registerSaveable(&b);
    a.hp = 42; a.x = 3.5f; b.hp = 7;
    CHECK(r.sys.saveSlot("slot1"));
    a.hp = 1; a.x = 0; b.hp = 1;                       // the game moves on...
    CHECK(r.sys.loadSlot("slot1"));                    // ...and we go back
    CHECK_EQ(a.hp, 42); CHECK_EQ(a.x, 3.5f); CHECK_EQ(b.hp, 7);
    CHECK(!fs::exists(r.dir + "/slot1.json.tmp"));
}

TEST(save_events_are_emitted) {
    Rig r("ev");
    std::string saved, loaded;
    r.eng.events.subscribe<core::GameSaved>([&](const core::GameSaved& e) { saved = e.slot; });
    r.eng.events.subscribe<core::GameLoaded>([&](const core::GameLoaded& e) { loaded = e.slot; });
    CHECK(r.sys.saveSlot("s"));
    CHECK(r.sys.loadSlot("s"));
    CHECK_EQ(saved, std::string("s"));
    CHECK_EQ(loaded, std::string("s"));
}

TEST(save_slot_names_are_validated) {
    Rig r("names");
    for (auto* bad : {"", "../evil", "a/b", "a b", "x.json", ".."}) {
        CHECK(!r.sys.saveSlot(bad));
        CHECK(!r.sys.loadSlot(bad));
    }
    CHECK(r.sys.saveSlot("ok_Name-1"));
}

TEST(save_list_is_newest_first_and_skips_corrupt_files) {
    Rig r("list");
    r.t = 100; CHECK(r.sys.saveSlot("old"));
    r.t = 300; CHECK(r.sys.saveSlot("newest"));
    r.t = 200; CHECK(r.sys.saveSlot("mid"));
    std::ofstream(r.dir + "/broken.json") << "{ nope";
    std::ofstream(r.dir + "/notes.txt") << "hi";
    auto l = r.sys.listSlots();
    CHECK_EQ(l.size(), (size_t)3);
    CHECK_EQ(l[0].name, std::string("newest"));
    CHECK_EQ(l[1].name, std::string("mid"));
    CHECK_EQ(l[2].name, std::string("old"));
    CHECK_EQ(l[0].time, 300LL);
}

TEST(save_load_failures_leave_the_game_untouched) {
    Rig r("bad");
    Thing a("m");
    r.sys.registerSaveable(&a);
    a.hp = 55;
    CHECK(!r.sys.loadSlot("missing"));
    std::ofstream(r.dir + "/corrupt.json") << "{ not json";
    CHECK(!r.sys.loadSlot("corrupt"));
    std::ofstream(r.dir + "/future.json") << R"({"format": 99, "time": 1, "modules": {"m": {"hp": 1}}})";
    CHECK(!r.sys.loadSlot("future"));
    std::ofstream(r.dir + "/nomods.json") << R"({"format": 1})";
    CHECK(!r.sys.loadSlot("nomods"));
    for (auto* f : {"corrupt", "future", "nomods"}) CHECK(fs::exists(r.dir + "/" + f + ".json"));   // they were rejected, not missing
    CHECK_EQ(a.hp, 55);
}

TEST(save_tolerates_old_and_new_saves) {
    Rig r("compat");
    Thing a("known");
    r.sys.registerSaveable(&a);
    a.hp = 9; a.x = 8;
    // save has: an unknown module (removed since), and only some keys of ours (older version)
    std::ofstream(r.dir + "/s.json") << R"({"format":1,"time":5,"modules":{"gone":{"z":1},"known":{"hp":77}}})";
    CHECK(r.sys.loadSlot("s"));
    CHECK_EQ(a.hp, 77);       // loaded
    CHECK_EQ(a.x, 8.0f);      // missing key keeps current value
    // a registered module with no entry in the save keeps its state
    Thing extra("brand_new"); extra.hp = 3;
    r.sys.registerSaveable(&extra);
    CHECK(r.sys.loadSlot("s"));
    CHECK_EQ(extra.hp, 3);
}

TEST(save_new_slot_names_are_unique) {
    Rig r("uniq");
    r.t = 1700000000;
    std::string n1 = r.sys.newSlotName();
    CHECK(core::SaveSystem::validSlotName(n1));
    CHECK(n1.rfind("save_", 0) == 0);
    CHECK(r.sys.saveSlot(n1));
    std::string n2 = r.sys.newSlotName();               // same second: must not overwrite
    CHECK(n1 != n2);
}

TEST(save_duplicate_ids_replace_and_unregister_works) {
    Rig r("dup");
    Thing a("same"), b("same");
    a.hp = 1; b.hp = 2;
    r.sys.registerSaveable(&a);
    r.sys.registerSaveable(&b);                         // replaces a
    CHECK(r.sys.saveSlot("s"));
    a.hp = 0; b.hp = 0;
    CHECK(r.sys.loadSlot("s"));
    CHECK_EQ(b.hp, 2);
    CHECK_EQ(a.hp, 0);                                  // a was replaced, so untouched
    r.sys.unregisterSaveable(&b);
    b.hp = 9;
    CHECK(r.sys.loadSlot("s"));
    CHECK_EQ(b.hp, 9);
}

TEST(save_delete_slot) {
    Rig r("del");
    CHECK(r.sys.saveSlot("x"));
    CHECK_EQ(r.sys.listSlots().size(), (size_t)1);
    CHECK(r.sys.deleteSlot("x"));
    CHECK_EQ(r.sys.listSlots().size(), (size_t)0);
    CHECK(!r.sys.deleteSlot("x"));
    CHECK(!r.sys.deleteSlot("../x"));
}

TEST(save_active_slot_follows_manual_saves_and_loads) {
    Rig r("active");
    CHECK(r.sys.activeSlot().empty());                  // fresh session: nothing to overwrite yet
    CHECK_EQ(r.sys.lastSaveTime(), 0LL);
    CHECK_EQ(r.sys.lastLoadTime(), 0LL);
    r.t = 500; CHECK(r.sys.saveSlot("a"));
    CHECK_EQ(r.sys.activeSlot(), std::string("a"));
    CHECK_EQ(r.sys.lastSaveTime(), 500LL);
    r.t = 600; CHECK(r.sys.saveSlot("b"));
    CHECK_EQ(r.sys.activeSlot(), std::string("b"));
    r.t = 700; CHECK(r.sys.loadSlot("a"));
    CHECK_EQ(r.sys.activeSlot(), std::string("a"));
    CHECK_EQ(r.sys.lastLoadTime(), 700LL);
    CHECK_EQ(r.sys.lastSaveTime(), 600LL);
    // failures change nothing
    CHECK(!r.sys.loadSlot("missing"));
    CHECK(!r.sys.saveSlot("../bad"));
    CHECK_EQ(r.sys.activeSlot(), std::string("a"));
    CHECK_EQ(r.sys.lastLoadTime(), 700LL);
    // deleting the active slot clears it; deleting another does not
    CHECK(r.sys.deleteSlot("b"));
    CHECK_EQ(r.sys.activeSlot(), std::string("a"));
    CHECK(r.sys.deleteSlot("a"));
    CHECK(r.sys.activeSlot().empty());
}

TEST(save_autosave_does_not_change_active_slot) {
    Rig r("auto");
    Thing a("m");
    r.sys.registerSaveable(&a);
    CHECK(r.sys.saveSlot("mine"));
    r.t = 2000; a.hp = 11;
    CHECK(r.sys.autosave());
    CHECK(fs::exists(r.dir + "/" + core::ISaveSystem::kAutosaveSlot + ".json"));
    CHECK_EQ(r.sys.activeSlot(), std::string("mine"));
    CHECK_EQ(r.sys.lastSaveTime(), 2000LL);             // an autosave still counts as "last saved"
    CHECK(r.sys.saveSlot(core::ISaveSystem::kAutosaveSlot));   // by hand into the reserved slot: still not active
    CHECK_EQ(r.sys.activeSlot(), std::string("mine"));
    a.hp = 0;
    CHECK(r.sys.loadSlot(core::ISaveSystem::kAutosaveSlot));   // loading the autosave: next "Save" acts like "Save As"
    CHECK_EQ(a.hp, 11);
    CHECK(r.sys.activeSlot().empty());
}

TEST(save_autosave_interval_triggers) {
    Rig r("interval");
    std::vector<std::string> saved;
    r.eng.events.subscribe<core::GameSaved>([&](const core::GameSaved& e) { saved.push_back(e.slot); });
    CHECK_EQ(r.sys.autosaveInterval(), 300.0f);         // default tunable
    r.sys.setAutosaveInterval(10.0f);
    for (int i = 0; i < 99; i++) r.sys.onFixedUpdate(r.eng, 0.1f);   // ~9.9 s
    CHECK(saved.empty());
    for (int i = 0; i < 2; i++) r.sys.onFixedUpdate(r.eng, 0.1f);    // crosses 10 s
    CHECK_EQ(saved.size(), (size_t)1);
    CHECK_EQ(saved[0], std::string(core::ISaveSystem::kAutosaveSlot));
    CHECK(r.sys.activeSlot().empty());
    // a manual save restarts the countdown
    for (int i = 0; i < 50; i++) r.sys.onFixedUpdate(r.eng, 0.1f);   // 5 s
    CHECK(r.sys.saveSlot("manual"));
    for (int i = 0; i < 90; i++) r.sys.onFixedUpdate(r.eng, 0.1f);   // 9 s since the manual save
    CHECK_EQ(saved.size(), (size_t)2);                  // only the manual save was added
    for (int i = 0; i < 11; i++) r.sys.onFixedUpdate(r.eng, 0.1f);
    CHECK_EQ(saved.size(), (size_t)3);
    CHECK_EQ(r.sys.activeSlot(), std::string("manual"));
    // 0 disables it
    r.sys.setAutosaveInterval(0);
    for (int i = 0; i < 1000; i++) r.sys.onFixedUpdate(r.eng, 1.0f);
    CHECK_EQ(saved.size(), (size_t)3);
}
