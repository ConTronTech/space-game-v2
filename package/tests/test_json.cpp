#include "engine/json.h"
#include "tests/test.h"

using engine::Json;

TEST(json_objects_arrays_and_types) {
    std::string err;
    Json j = Json::parse(R"({"a": 1.5, "b": [1, 2, 3], "c": {"d": true, "e": "hi"}, "n": null})", &err);
    CHECK(j.isObject());
    CHECK(err.empty());
    CHECK_EQ(j["a"].num(), 1.5);
    CHECK_EQ(j["b"].size(), (size_t)3);
    CHECK_EQ(j["b"].at(2).num(), 3.0);
    CHECK(j["c"]["d"].boolean());
    CHECK_EQ(j["c"]["e"].str(), std::string("hi"));
    CHECK(j["n"].isNull());
    CHECK_EQ(j.keys().size(), (size_t)4);
    CHECK_EQ(j.keys()[0], std::string("a")); // file order preserved
}

TEST(json_missing_lookups_use_defaults) {
    Json j = Json::parse(R"({"a": 1})");
    CHECK(j["nope"].isNull());
    CHECK_EQ(j["nope"].num(7), 7.0);
    CHECK_EQ(j["nope"]["deeper"].str("x"), std::string("x"));
    CHECK_EQ(j["a"].str("fallback"), std::string("fallback")); // wrong type -> default
    CHECK(j.at(0).isNull());
}

TEST(json_comments_and_whitespace) {
    Json j = Json::parse("// header\n{ \"a\": 1, // trailing\n \"b\": 2 }\n// end");
    CHECK(j.isObject());
    CHECK_EQ(j["b"].num(), 2.0);
}

TEST(json_string_escapes) {
    Json j = Json::parse(R"({"s": "a\"b\\c\ndé"})");
    CHECK_EQ(j["s"].str(), std::string("a\"b\\c\nd\xc3\xa9"));
}

TEST(json_numbers) {
    Json j = Json::parse("[-3, 0.25, 1e3, 2.5E-1]");
    CHECK_EQ(j.at(0).num(), -3.0);
    CHECK_EQ(j.at(1).num(), 0.25);
    CHECK_EQ(j.at(2).num(), 1000.0);
    CHECK_EQ(j.at(3).num(), 0.25);
}

TEST(json_errors_report_and_return_null) {
    const char* bad[] = {"{", "[1,]", "{\"a\" 1}", "{\"a\": }", "\"open", "{} extra", "{'a': 1}", ""};
    for (auto* s : bad) {
        std::string err;
        Json j = Json::parse(s, &err);
        CHECK(j.isNull());
        CHECK(!err.empty());
    }
}

TEST(json_error_has_line_number) {
    std::string err;
    Json::parse("{\n\n  \"a\": ]\n}", &err);
    CHECK(err.find("line 3") != std::string::npos);
}

TEST(json_deep_nesting_is_rejected_not_crashed) {
    std::string deep(200, '[');
    std::string err;
    Json j = Json::parse(deep, &err);
    CHECK(j.isNull());
    CHECK(!err.empty());
}

// ---- writer ----
TEST(json_dump_builds_and_roundtrips) {
    Json o = Json::object();
    o.set("name", "ship \"one\"\n").set("hp", 80).set("ratio", 0.3f).set("alive", true)
     .set("pos", Json::array().push(1.5).push(-2).push(3.25))
     .set("nested", Json::object().set("a", Json::array().push(Json::object().set("k", 1)).push(Json::object())));
    std::string text = o.dump();
    std::string err;
    Json back = Json::parse(text, &err);
    CHECK(err.empty());
    CHECK_EQ(back["name"].str(), std::string("ship \"one\"\n"));
    CHECK_EQ(back["hp"].num(), 80.0);
    CHECK_EQ(back["ratio"].num(), 0.3);              // float 0.3f is saved as 0.3, not 0.300000011920929
    CHECK(back["alive"].boolean());
    CHECK_EQ(back["pos"].at(1).num(), -2.0);
    CHECK_EQ(back["nested"]["a"].at(0)["k"].num(), 1.0);
    CHECK(back["nested"]["a"].at(1).isObject());
    CHECK_EQ(back.dump(), text);                     // stable: dump(parse(dump(x))) == dump(x)
}

TEST(json_dump_number_formats) {
    CHECK_EQ(Json(3).dump(), std::string("3\n"));
    CHECK_EQ(Json(-7).dump(), std::string("-7\n"));
    CHECK_EQ(Json(0.5).dump(), std::string("0.5\n"));
    CHECK_EQ(Json(0.1f).dump(), std::string("0.1\n"));
    CHECK_EQ(Json(1e20).dump().substr(0, 5), std::string("1e+20"));
    CHECK_EQ(Json(std::nan("")).dump(), std::string("null\n"));   // NaN/inf are not valid JSON
    double big = 123456789.123456789;
    CHECK_EQ(Json::parse(Json(big).dump()).num(), big);            // doubles round-trip exactly
}

TEST(json_dump_set_replaces_and_control_chars_escape) {
    Json o = Json::object();
    o.set("a", 1).set("a", 2).set("s", std::string("tab\there\x01"));
    CHECK_EQ(o.keys().size(), (size_t)1 + 1);
    Json back = Json::parse(o.dump());
    CHECK_EQ(back["a"].num(), 2.0);
    CHECK_EQ(back["s"].str(), std::string("tab\there\x01"));
}

TEST(json_dump_empty_containers) {
    Json o = Json::object();
    o.set("o", Json::object()).set("a", Json::array());
    Json back = Json::parse(o.dump());
    CHECK(back["o"].isObject());
    CHECK(back["a"].isArray());
    CHECK_EQ(back["a"].size(), (size_t)0);
}
