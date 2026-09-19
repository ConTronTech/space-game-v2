#include <filesystem>
#include <fstream>
#include <sstream>
#include "engine/config.h"
#include "tests/test.h"

namespace fs = std::filesystem;

static std::string tmpPath(const char* name) { return (fs::temp_directory_path() / (std::string("sgv2_test_") + name)).string(); }
static void writeFile(const std::string& p, const std::string& s) { std::ofstream(p) << s; }
static std::string readFile(const std::string& p) { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }

TEST(config_no_file_returns_defaults) {
    std::string p = tmpPath("none.json");
    fs::remove(p);
    engine::Config c(p);
    c.load();
    CHECK_EQ(c.get("flight.drift", 1.0f), 1.0f);
    CHECK_EQ(c.get("flight.name", std::string("ship")), std::string("ship"));
    CHECK_EQ(c.get("flight.assist", false), false);
    CHECK_EQ(c.get("engine.count", 7), 7);
}

TEST(config_override_and_DEFAULT_keyword) {
    std::string p = tmpPath("over.json");
    writeFile(p, R"({"flight": {"drift": 0.25, "thrust": "DEFAULT", "assist": true, "name": "x"}})");
    engine::Config c(p);
    c.load();
    CHECK_EQ(c.get("flight.drift", 1.0f), 0.25f);
    CHECK_EQ(c.get("flight.thrust", 40.0f), 40.0f);     // "DEFAULT" -> built-in
    CHECK_EQ(c.get("flight.assist", false), true);
    CHECK_EQ(c.get("flight.name", std::string("d")), std::string("x"));
    fs::remove(p);
}

TEST(config_wrong_type_falls_back) {
    std::string p = tmpPath("wrong.json");
    writeFile(p, R"({"a": {"n": "fast", "b": 3, "s": 9}})");
    engine::Config c(p);
    c.load();
    CHECK_EQ(c.get("a.n", 40.0f), 40.0f);
    CHECK_EQ(c.get("a.b", true), true);
    CHECK_EQ(c.get("a.s", std::string("d")), std::string("d"));
    fs::remove(p);
}

TEST(config_broken_json_uses_defaults) {
    std::string p = tmpPath("broken.json");
    writeFile(p, "{ nope");
    engine::Config c(p);
    c.load();
    CHECK_EQ(c.get("x.y", 3), 3);
    fs::remove(p);
}

TEST(config_save_writes_defaults_with_comments_and_keeps_edits) {
    std::string p = tmpPath("save.json");
    fs::remove(p);
    {
        engine::Config c(p);
        c.load();
        c.get("flight.drift", 1.0f, "how floaty");
        c.get("flight.thrust", 40.0f);
        c.save();
    }
    std::string text = readFile(p);
    CHECK(text.find("\"drift\": \"DEFAULT\"") != std::string::npos);
    CHECK(text.find("default 1.0 - how floaty") != std::string::npos);
    CHECK(text.find("default 40.0") != std::string::npos);

    // the user edits one value; a later run adds a new tunable; the edit must survive
    size_t at = text.find("\"drift\": \"DEFAULT\"");
    text.replace(at, std::string("\"drift\": \"DEFAULT\"").size(), "\"drift\": 0.4");
    writeFile(p, text);
    {
        engine::Config c(p);
        c.load();
        CHECK_EQ(c.get("flight.drift", 1.0f, "how floaty"), 0.4f);
        c.get("flight.max_speed", 0.0f); // new key -> file rewritten
        c.save();
    }
    text = readFile(p);
    CHECK(text.find("\"drift\": 0.4") != std::string::npos);
    CHECK(text.find("\"max_speed\": \"DEFAULT\"") != std::string::npos);
    {   // and the file we wrote parses cleanly back
        engine::Config c(p);
        c.load();
        CHECK_EQ(c.get("flight.drift", 1.0f), 0.4f);
        CHECK_EQ(c.get("flight.max_speed", 5.0f), 5.0f);
    }
    fs::remove(p);
}

TEST(config_save_is_skipped_when_nothing_new) {
    std::string p = tmpPath("clean.json");
    writeFile(p, R"({"a": {"b": 2}})");
    engine::Config c(p);
    c.load();
    c.get("a.b", 1);   // already in the file
    c.save();
    CHECK_EQ(readFile(p), std::string(R"({"a": {"b": 2}})")); // untouched, comments/format intact
    fs::remove(p);
}
