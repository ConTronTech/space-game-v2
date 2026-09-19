#include <filesystem>
#include <fstream>
#include <sstream>
#include "core/settings/settings.h"
#include "engine/engine.h"
#include "tests/test.h"

namespace fs = std::filesystem;
static std::string tmp(const char* n) { return (fs::temp_directory_path() / (std::string("sgv2_set_") + n)).string(); }

TEST(settings_typed_get_set_and_defaults) {
    engine::Engine eng;
    core::Settings s;
    s.setPath(tmp("a.json")); fs::remove(tmp("a.json"));
    s.init(eng);
    CHECK_EQ(s.get("video.fov", 90.0f), 90.0f);             // unset -> caller's default
    s.set("video.fov", 105.0f);
    s.set("video.fullscreen", true);
    s.set("player.name", std::string("Kepler"));
    CHECK_EQ(s.get("video.fov", 90.0f), 105.0f);
    CHECK_EQ(s.get("video.fullscreen", false), true);
    CHECK_EQ(s.get("player.name", std::string("x")), std::string("Kepler"));
    CHECK_EQ(s.get("video.fov", std::string("zz")), std::string("zz"));   // wrong type -> default
    s.shutdown(eng);
    fs::remove(tmp("a.json"));
}

TEST(settings_emit_changed_only_on_real_change) {
    engine::Engine eng;
    core::Settings s;
    s.setPath(tmp("b.json")); fs::remove(tmp("b.json"));
    s.init(eng);
    std::vector<std::string> keys;
    eng.events.subscribe<core::SettingChanged>([&](const core::SettingChanged& e) { keys.push_back(e.key); });
    s.set("a", 1.0f);
    s.set("a", 1.0f);          // same value: no event
    s.set("a", 2.0f);
    s.set("b", true);
    CHECK_EQ(keys.size(), (size_t)3);
    CHECK_EQ(keys[0], std::string("a"));
    CHECK_EQ(keys[2], std::string("b"));
    s.shutdown(eng);
    fs::remove(tmp("b.json"));
}

TEST(settings_persist_across_runs_and_write_atomically) {
    std::string p = tmp("c.json");
    fs::remove(p);
    {
        engine::Engine eng;
        core::Settings s;
        s.setPath(p);
        s.init(eng);
        s.set("video.fov", 110.0f);
        s.set("input.mouse_sensitivity", 1.5f);
        s.shutdown(eng);                                    // dirty -> saved on shutdown
    }
    CHECK(fs::exists(p));
    CHECK(!fs::exists(p + ".tmp"));                         // temp file was renamed away
    {
        engine::Engine eng;
        core::Settings s;
        s.setPath(p);
        s.init(eng);
        CHECK_EQ(s.get("video.fov", 90.0f), 110.0f);
        CHECK_EQ(s.get("input.mouse_sensitivity", 1.0f), 1.5f);
        s.shutdown(eng);
    }
    std::ifstream f(p); std::stringstream ss; ss << f.rdbuf();
    CHECK(ss.str().find("\"input.mouse_sensitivity\": 1.5") != std::string::npos);   // 1.5, not 1.5000000
    fs::remove(p);
}

TEST(settings_corrupt_file_falls_back_to_defaults) {
    std::string p = tmp("d.json");
    std::ofstream(p) << "{ not json";
    engine::Engine eng;
    core::Settings s;
    s.setPath(p);
    s.init(eng);
    CHECK_EQ(s.get("video.fov", 90.0f), 90.0f);
    s.set("video.fov", 100.0f);                             // and it recovers: next save replaces the bad file
    s.shutdown(eng);
    engine::Engine eng2;
    core::Settings s2;
    s2.setPath(p);
    s2.init(eng2);
    CHECK_EQ(s2.get("video.fov", 90.0f), 100.0f);
    s2.shutdown(eng2);
    fs::remove(p);
}
