#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <unistd.h>
#include "ship/cockpit/cockpit_light.h"
#include "ship/cockpit/hud_layout.h"
#include "ship/cockpit/ship_registry.h"
#include "ship/cockpit/stroke_font.h"
#include "tests/test.h"

namespace {
bool nearf(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
struct CockpitTmp {
    std::filesystem::path dir;
    CockpitTmp() {
        static int n = 0;
        dir = std::filesystem::temp_directory_path() / ("sg2_cockpit_" + std::to_string(::getpid()) + "_" + std::to_string(n++));
        std::filesystem::create_directories(dir);
    }
    ~CockpitTmp() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
    void ship(const std::string& folder, const std::string& json) {
        std::filesystem::create_directories(dir / folder);
        std::ofstream(dir / folder / "ship.json") << json;
    }
};
} // namespace

// ---- stroke font ----
TEST(font_letters_and_digits_have_glyphs) {
    for (char c = 'A'; c <= 'Z'; c++) CHECK(cockpit::hasGlyph(c));
    for (char c = '0'; c <= '9'; c++) CHECK(cockpit::hasGlyph(c));
    for (char c : std::string("+-.,:;/%!?_=()<>|'*")) CHECK(cockpit::hasGlyph(c));
    CHECK(!cockpit::hasGlyph(' '));
    CHECK(!cockpit::hasGlyph('\x01'));
    CHECK(!cockpit::hasGlyph((char)0xC3));       // no crash on bytes above 127 either
}
TEST(font_lower_case_draws_as_upper) {
    CHECK_EQ(cockpit::glyphStrokes('a').size(), cockpit::glyphStrokes('A').size());
    CHECK(!cockpit::glyphStrokes('q').empty());
}
TEST(font_strokes_stay_inside_the_cell) {
    for (int c = 32; c < 127; c++)
        for (auto& s : cockpit::glyphStrokes((char)c)) {
            CHECK(s.x1 >= 0 && s.x1 <= 1 && s.x2 >= 0 && s.x2 <= 1);
            CHECK(s.y1 >= 0 && s.y1 <= 1.06f && s.y2 >= 0 && s.y2 <= 1.06f);   // ',' hangs slightly below the baseline
        }
}
TEST(font_text_width_matches_layout) {
    CHECK(nearf(cockpit::textWidth("", 0.1f), 0.0f));
    float h = 0.2f, one = cockpit::textWidth("A", h);
    CHECK(nearf(one, h * cockpit::kCellAspect));
    CHECK(nearf(cockpit::textWidth("ABC", h), cockpit::textAdvance(h) * 2 + one));
    CHECK(cockpit::textWidth("ABCD", h) > cockpit::textWidth("ABC", h));
}
TEST(font_layout_places_and_scales_strokes) {
    std::vector<cockpit::Stroke> out;
    cockpit::layoutText("I I", 1.0f, 2.0f, 0.5f, out);
    CHECK_EQ(out.size(), cockpit::glyphStrokes('I').size() * 2);      // the space adds nothing
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (auto& s : out) {
        minX = std::min({minX, s.x1, s.x2}); maxX = std::max({maxX, s.x1, s.x2});
        minY = std::min({minY, s.y1, s.y2}); maxY = std::max({maxY, s.y1, s.y2});
    }
    CHECK(minX >= 1.0f - 1e-4f && maxX <= 1.0f + cockpit::textWidth("I I", 0.5f) + 1e-4f);
    CHECK(nearf(minY, 2.0f) && nearf(maxY, 2.5f));
}

// ---- hud layout ----
TEST(hud_fraction_and_colours) {
    CHECK(nearf(cockpit::fraction(35, 100), 0.35f));
    CHECK(nearf(cockpit::fraction(500, 100), 1.0f));
    CHECK(nearf(cockpit::fraction(-5, 100), 0.0f));
    CHECK(nearf(cockpit::fraction(5, 0), 0.0f));                  // no divide by zero
    CHECK(cockpit::levelForFraction(0.9f) == cockpit::Level::Ok);
    CHECK(cockpit::levelForFraction(0.4f) == cockpit::Level::Warn);
    CHECK(cockpit::levelForFraction(0.1f) == cockpit::Level::Bad);
    CHECK(cockpit::barColor(0.1f).r > cockpit::barColor(0.1f).g);   // low = red
    CHECK(cockpit::barColor(0.9f).g > cockpit::barColor(0.9f).r);   // healthy = green
}
TEST(hud_systems_lines_reflect_status) {
    ship::ShipStatus s;
    s.hp = 35; s.maxHp = 100; s.warpFuel = 10; s.maxWarpFuel = 100;
    auto lines = cockpit::systemsLines(s);
    CHECK_EQ(lines.size(), (size_t)5);
    CHECK_EQ(lines[0].text, std::string("HULL 35/100"));
    CHECK(lines[0].level == cockpit::Level::Warn);
    CHECK_EQ(lines[1].text, std::string("SHIELD NOT FITTED"));
    CHECK(lines[1].level == cockpit::Level::Dim);
    CHECK_EQ(lines[2].text, std::string("WARP FUEL 10/100"));
    CHECK(lines[2].level == cockpit::Level::Bad);
    CHECK_EQ(lines[3].text, std::string("DRIVE STANDBY"));
    CHECK_EQ(lines[4].text, std::string("STATUS ALIVE"));

    s.shieldInstalled = true; s.shieldEnabled = false;
    CHECK_EQ(cockpit::systemsLines(s)[1].text, std::string("SHIELD DISABLED"));
    s.shieldEnabled = true; s.shield = 120; s.maxShield = 200;
    CHECK_EQ(cockpit::systemsLines(s)[1].text, std::string("SHIELD 120/200"));
    s.shield = 0;
    CHECK_EQ(cockpit::systemsLines(s)[1].text, std::string("SHIELD DOWN"));
    s.warping = true; s.alive = false;
    auto dead = cockpit::systemsLines(s);
    CHECK_EQ(dead[3].text, std::string("DRIVE ENGAGED"));
    CHECK_EQ(dead[4].text, std::string("STATUS DESTROYED"));
    CHECK(dead[4].level == cockpit::Level::Bad);
    for (auto& l : dead) CHECK(l.text.size() <= 20);                 // must fit the screen
}
TEST(hud_number_text) {
    CHECK_EQ(cockpit::speedText(87.4f), std::string("87"));
    CHECK_EQ(cockpit::speedText(-3.0f), std::string("0"));
    CHECK_EQ(cockpit::percentText(0.351f), std::string("35%"));
    CHECK_EQ(cockpit::percentText(2.0f), std::string("100%"));
    CHECK_EQ(cockpit::headingText({0, 0, -1}), std::string("HDG 000  PIT +0"));    // -Z is north
    CHECK_EQ(cockpit::headingText({1, 0, 0}), std::string("HDG 090  PIT +0"));
    CHECK_EQ(cockpit::headingText({0, 0, 1}), std::string("HDG 180  PIT +0"));
    CHECK_EQ(cockpit::headingText({-1, 0, 0}), std::string("HDG 270  PIT +0"));
    CHECK_EQ(cockpit::headingText({0, 1, 0}), std::string("HDG 000  PIT +90"));
    CHECK_EQ(cockpit::headingText({-0.0001f, 0, -1}), std::string("HDG 000  PIT +0"));   // 359.99 rounds to 000, not 360
}

// ---- light tunable ----
TEST(light_dir_parsing) {
    engine::Vec3 v{9, 9, 9};
    CHECK(cockpit::parseVec3("0.3, 0.8 ,-0.5", v));
    CHECK(nearf(v.x, 0.3f) && nearf(v.y, 0.8f) && nearf(v.z, -0.5f));
    engine::Vec3 keep{1, 2, 3};
    CHECK(!cockpit::parseVec3("1,2", keep));
    CHECK(!cockpit::parseVec3("1,2,3,4", keep));
    CHECK(!cockpit::parseVec3("a,b,c", keep));
    CHECK(!cockpit::parseVec3("0,0,0", keep));
    CHECK(!cockpit::parseVec3("", keep));
    CHECK(nearf(keep.x, 1.0f) && nearf(keep.z, 3.0f));               // untouched on failure
}

// ---- ship registry ----
TEST(ship_json_fields_and_defaults) {
    cockpit::ShipDef d;
    CHECK(cockpit::parseShipJson(R"({"name":"ShipV2","model":"ShipV2.obj","showDefaultUI":false,
        "screens":[{"tag":"@HUD-INFO","content":"FLIGHT_DATA"},{"tag":"@HUD-RADAR","content":"PROXIMITY_RADAR"},{"content":"x"}]})", "shipv2", d));
    CHECK_EQ(d.name, std::string("ShipV2"));
    CHECK_EQ(d.model, std::string("ShipV2.obj"));
    CHECK(!d.showDefaultUI);
    CHECK_EQ(d.screens.size(), (size_t)2);                            // the entry without a tag is dropped
    CHECK(d.screenForTag("@HUD-INFO") != nullptr);
    CHECK_EQ(d.screenForTag("@HUD-RADAR")->content, std::string("PROXIMITY_RADAR"));
    CHECK(d.screenForTag("@HUD-NOPE") == nullptr);

    cockpit::ShipDef bare;
    CHECK(cockpit::parseShipJson("{}", "myfolder", bare));
    CHECK_EQ(bare.name, std::string("myfolder"));
    CHECK_EQ(bare.model, std::string("ship.obj"));
    CHECK(bare.showDefaultUI);
    CHECK(bare.screens.empty());

    std::string err;
    cockpit::ShipDef bad;
    CHECK(!cockpit::parseShipJson("[1,2]", "x", bad, &err));
    CHECK(!cockpit::parseShipJson("{ nope", "x", bad, &err));
    CHECK(!err.empty());
}
TEST(ship_scan_and_choose) {
    CockpitTmp t;
    t.ship("shipv2", R"({"name":"ShipV2","model":"ShipV2.obj","showDefaultUI":false,"screens":[{"tag":"@HUD-INFO","content":"FLIGHT_DATA"}]})");
    t.ship("shipv1", R"({"name":"ShipV1","model":"ship.obj","showDefaultUI":true,"screens":[]})");
    t.ship("broken", "{ not json");
    std::filesystem::create_directories(t.dir / "no_json_here");
    std::ofstream(t.dir / "stray.txt") << "x";

    std::vector<std::string> warns;
    auto ships = cockpit::scanShips(t.dir.string(), &warns);
    CHECK_EQ(ships.size(), (size_t)2);
    CHECK_EQ(warns.size(), (size_t)1);                                // only the unparsable ship.json is reported
    CHECK_EQ(ships[0].name, std::string("ShipV1"));                   // sorted by folder name: deterministic "first"

    bool fell = true;
    auto* v2 = cockpit::chooseShip(ships, "shipv2", &fell);           // case-insensitive
    CHECK(v2 && v2->name == "ShipV2" && !fell);
    CHECK(v2 && !v2->showDefaultUI);
    auto* fb = cockpit::chooseShip(ships, "Nonexistent", &fell);
    CHECK(fb && fb->name == "ShipV1" && fell);                        // falls back to the first one
    CHECK(cockpit::chooseShip({}, "ShipV2", &fell) == nullptr);
}
TEST(ship_scan_missing_folder_is_empty) {
    CHECK(cockpit::scanShips("/nonexistent/sg2/ships").empty());
}
