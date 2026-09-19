#include <filesystem>
#include <fstream>
#include <sstream>
#include "engine/log.h"
#include "tests/test.h"

namespace fs = std::filesystem;

static std::string slurp(const std::string& p) { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }

TEST(log_level_filters_and_file_gets_lines) {
    std::string p = (fs::temp_directory_path() / "sgv2_test_log" / "game.log").string();  // folder is created for us
    fs::remove_all(fs::path(p).parent_path());
    CHECK(engine::log::openFile(p));
    engine::log::setLevel(engine::log::Level::Warn);
    LOG_D("t", "debug hidden %d", 1);
    LOG_I("t", "info hidden %d", 2);
    LOG_W("t", "warn shown %d", 3);
    LOG_E("t", "error shown %s", "x");
    engine::log::openFile("");                 // closes + flushes
    std::string text = slurp(p);
    CHECK(text.find("hidden") == std::string::npos);
    CHECK(text.find("WARN  [t] warn shown 3") != std::string::npos);
    CHECK(text.find("ERROR [t] error shown x") != std::string::npos);
    engine::log::setLevel(engine::log::Level::Info);
    fs::remove_all(fs::path(p).parent_path());
}

TEST(log_level_names) {
    CHECK(engine::log::setLevel("debug"));
    CHECK(engine::log::level() == engine::log::Level::Debug);
    CHECK(!engine::log::setLevel("loud"));
    CHECK(engine::log::level() == engine::log::Level::Debug);   // unchanged on bad name
    engine::log::setLevel("info");
}
