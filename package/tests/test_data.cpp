#include <filesystem>
#include <fstream>
#include "core/data_registry/data_registry.h"
#include "engine/engine.h"
#include "tests/test.h"

namespace fs = std::filesystem;

namespace {
struct DataRig {
    std::string dir;
    engine::Engine eng;
    core::DataRegistry reg;
    explicit DataRig(const char* name) : dir((fs::temp_directory_path() / (std::string("sgv2_data_") + name)).string()) {
        fs::remove_all(dir);
        fs::create_directories(dir);
        reg.setDir(dir);
    }
    ~DataRig() { fs::remove_all(dir); }
    void write(const std::string& rel, const std::string& text) {
        fs::create_directories(fs::path(dir + "/" + rel).parent_path());
        std::ofstream(dir + "/" + rel) << text;
    }
    void load() { reg.init(eng); }
};
} // namespace

TEST(data_file_and_folder_categories_and_order) {
    DataRig r("cats");
    r.write("ores.json", R"({"iron": {"rarity": 40}, "gold": {"rarity": 8}})");
    r.write("items/a.json", R"({"kit": {"name": "Kit"}})");
    r.write("items/b.json", R"({"scanner": {"name": "Scanner"}})");
    r.load();
    CHECK(r.reg.has("ores", "iron"));
    CHECK_EQ(r.reg.get("ores", "gold")["rarity"].num(), 8.0);
    auto ores = r.reg.ids("ores");
    CHECK_EQ(ores.size(), (size_t)2);
    CHECK_EQ(ores[0], std::string("iron"));            // definition (file) order, not alphabetical
    CHECK_EQ(r.reg.ids("items").size(), (size_t)2);    // two files merge into one category
    CHECK_EQ(r.reg.categories().size(), (size_t)2);
}

TEST(data_later_files_override_earlier_ones) {
    DataRig r("mod");
    r.write("ores/base.json", R"({"iron": {"rarity": 40, "name": "Iron"}, "gold": {"rarity": 8}})");
    r.write("ores/zz_mod.json", R"({"iron": {"rarity": 99}, "adamantium": {"rarity": 1}})");
    r.load();
    CHECK_EQ(r.reg.get("ores", "iron")["rarity"].num(), 99.0);
    CHECK(r.reg.get("ores", "iron")["name"].isNull());   // replaced wholesale, not merged
    CHECK(r.reg.has("ores", "adamantium"));
    CHECK_EQ(r.reg.ids("ores").size(), (size_t)3);        // override does not duplicate the id
    CHECK_EQ(r.reg.ids("ores")[0], std::string("iron"));
}

TEST(data_bad_input_is_skipped_not_fatal) {
    DataRig r("bad");
    r.write("ores.json", R"({"_comment": "ignored", "iron": {"rarity": 1}, "oops": 5, "arr": [1,2]})");
    r.write("items.json", "{ not json");
    r.write("readme.txt", "not data");
    r.load();
    CHECK_EQ(r.reg.ids("ores").size(), (size_t)1);        // comment, number and array entries dropped
    CHECK(!r.reg.has("ores", "_comment"));
    CHECK_EQ(r.reg.ids("items").size(), (size_t)0);       // corrupt file skipped
    CHECK(r.reg.get("ores", "nope").isNull());            // missing lookups are null, never a crash
    CHECK(r.reg.get("nothing", "nope").isNull());
    CHECK_EQ(r.reg.get("ores", "nope")["x"].num(7), 7.0); // ...and safe to chain
}

TEST(data_reload_picks_up_changes_and_emits_event) {
    DataRig r("reload");
    r.write("ores.json", R"({"iron": {"rarity": 1}})");
    r.load();
    int events = 0;
    r.eng.events.subscribe<core::DataReloaded>([&](const core::DataReloaded&) { events++; });
    r.write("ores.json", R"({"iron": {"rarity": 2}, "gold": {"rarity": 3}})");
    CHECK(r.reg.reload());
    CHECK_EQ(events, 1);
    CHECK_EQ(r.reg.get("ores", "iron")["rarity"].num(), 2.0);
    CHECK(r.reg.has("ores", "gold"));
}

TEST(data_missing_folder_is_not_fatal) {
    engine::Engine eng;
    core::DataRegistry reg;
    reg.setDir("/definitely/not/here");
    CHECK(reg.init(eng));
    CHECK_EQ(reg.ids("ores").size(), (size_t)0);
}

// ---- lint of the SHIPPED data/ (tests run from the repo root): a typo in content fails the build ----
TEST(shipped_data_is_consistent) {
    engine::Engine eng;
    core::DataRegistry reg;
    reg.setDir("data");
    reg.init(eng);
    CHECK(reg.ids("ores").size() >= 8);
    CHECK(reg.ids("items").size() >= 8);
    CHECK(reg.ids("recipes").size() >= 8);

    for (auto& id : reg.ids("ores")) {
        const auto& o = reg.get("ores", id);
        CHECK(!o["name"].str().empty());
        CHECK(o["rarity"].num() > 0);
        CHECK_EQ(o["color"].size(), (size_t)3);
        for (size_t k = 0; k < 3; k++) { double c = o["color"].at(k).num(-1); CHECK(c >= 0 && c <= 1); }
    }
    for (auto& id : reg.ids("items")) {
        const auto& it = reg.get("items", id);
        CHECK(!it["name"].str().empty());
        CHECK(!it["description"].str().empty());
        CHECK(it["effect"].isObject());
    }
    for (auto& id : reg.ids("recipes")) {
        const auto& r = reg.get("recipes", id);
        CHECK(reg.has("items", r["result"].str()));               // result must be a real item
        CHECK(r["ingredients"].isObject());
        CHECK(!r["ingredients"].keys().empty());
        for (auto& ore : r["ingredients"].keys()) {
            CHECK(reg.has("ores", ore));                           // every ingredient must be a real ore
            CHECK(r["ingredients"][ore].num() >= 1);
        }
    }
}
