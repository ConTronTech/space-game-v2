#pragma once
// Minimal JSON reader (no deps). Supports objects, arrays, strings, numbers, bool, null,
// plus // line comments so config files can be annotated. Lookups never throw:
// a missing key/index yields a null Json whose str()/num()/boolean() return the default.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace engine {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    static Json parse(std::string_view text, std::string* error = nullptr);

    // ---- building (for saving) ----
    //   Json o = Json::object();  o.set("hp", 80).set("name", "ship").set("pos", Json::array().push(1).push(2));
    //   std::string text = o.dump();
    Json() = default;
    Json(bool b) : t_(Type::Bool), b_(b) {}
    Json(const char* s) : t_(Type::String), s_(s ? s : "") {}
    Json(std::string s) : t_(Type::String), s_(std::move(s)) {}
    template <class T, class = std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>>>
    Json(T v) : t_(Type::Number), n_(shortest(v)) {}
    static Json object() { Json j; j.t_ = Type::Object; return j; }
    static Json array() { Json j; j.t_ = Type::Array; return j; }
    Json& set(const std::string& key, Json v) {          // object member (replaces an existing key)
        if (t_ != Type::Object) { *this = object(); }
        for (size_t i = 0; i < keys_.size(); i++) if (keys_[i] == key) { items_[i] = std::move(v); return *this; }
        keys_.push_back(key);
        items_.push_back(std::move(v));
        return *this;
    }
    Json& push(Json v) {                                  // array element
        if (t_ != Type::Array) { *this = array(); }
        items_.push_back(std::move(v));
        return *this;
    }
    std::string dump(int indent = 2) const { std::string out; write(out, indent, 0); out += "\n"; return out; }

    Type type() const { return t_; }
    bool isNull() const { return t_ == Type::Null; }
    bool isObject() const { return t_ == Type::Object; }
    bool isArray() const { return t_ == Type::Array; }

    bool has(std::string_view key) const { return find(key) != nullptr; }
    const Json& operator[](std::string_view key) const { auto* p = find(key); return p ? *p : null(); }
    const Json& at(size_t i) const { return (t_ == Type::Array && i < items_.size()) ? items_[i] : null(); }
    size_t size() const { return items_.size(); }
    const std::vector<std::string>& keys() const { return keys_; } // object keys, file order

    std::string str(std::string def = "") const { return t_ == Type::String ? s_ : def; }
    double num(double def = 0) const { return t_ == Type::Number ? n_ : def; }
    bool boolean(bool def = false) const { return t_ == Type::Bool ? b_ : def; }

private:
    friend struct JsonParser;

    // A float such as 0.3f must be saved as 0.3, not 0.30000001192...: use the shortest text that round-trips.
    template <class T> static double shortest(T v) {
        if constexpr (std::is_same_v<T, float>) {
            char b[40];
            for (int p = 1; p <= 9; p++) {
                std::snprintf(b, sizeof b, "%.*g", p, (double)v);
                if ((float)std::strtod(b, nullptr) == v) return std::strtod(b, nullptr);
            }
        }
        return (double)v;
    }
    static void quote(std::string& out, const std::string& s) {
        out += '"';
        for (unsigned char c : s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                    else out += (char)c;
            }
        }
        out += '"';
    }
    static std::string number(double d) {
        if (!std::isfinite(d)) return "null";
        char b[40];
        if (d == std::floor(d) && std::fabs(d) < 1e15) { std::snprintf(b, sizeof b, "%lld", (long long)d); return b; }
        for (int p = 6; p <= 17; p++) {                    // shortest text that reads back exactly
            std::snprintf(b, sizeof b, "%.*g", p, d);
            if (std::strtod(b, nullptr) == d) break;
        }
        return b;
    }
    bool scalar() const { return t_ != Type::Array && t_ != Type::Object; }
    void write(std::string& out, int indent, int depth) const {
        switch (t_) {
            case Type::Null: out += "null"; return;
            case Type::Bool: out += b_ ? "true" : "false"; return;
            case Type::Number: out += number(n_); return;
            case Type::String: quote(out, s_); return;
            case Type::Array: {
                if (items_.empty()) { out += "[]"; return; }
                bool inlineArr = items_.size() <= 16;
                for (auto& it : items_) if (!it.scalar()) inlineArr = false;
                if (inlineArr) {                          // short lists of numbers/strings stay on one line
                    out += '[';
                    for (size_t i = 0; i < items_.size(); i++) { if (i) out += ", "; items_[i].write(out, indent, depth + 1); }
                    out += ']';
                    return;
                }
                out += "[\n";
                for (size_t i = 0; i < items_.size(); i++) {
                    out.append((size_t)(depth + 1) * indent, ' ');
                    items_[i].write(out, indent, depth + 1);
                    out += i + 1 < items_.size() ? ",\n" : "\n";
                }
                out.append((size_t)depth * indent, ' ');
                out += ']';
                return;
            }
            case Type::Object: {
                if (items_.empty()) { out += "{}"; return; }
                out += "{\n";
                for (size_t i = 0; i < items_.size(); i++) {
                    out.append((size_t)(depth + 1) * indent, ' ');
                    quote(out, keys_[i]);
                    out += ": ";
                    items_[i].write(out, indent, depth + 1);
                    out += i + 1 < items_.size() ? ",\n" : "\n";
                }
                out.append((size_t)depth * indent, ' ');
                out += '}';
                return;
            }
        }
    }
    static const Json& null() { static const Json n; return n; }
    const Json* find(std::string_view key) const {
        if (t_ != Type::Object) return nullptr;
        for (size_t i = 0; i < keys_.size(); i++) if (keys_[i] == key) return &items_[i];
        return nullptr;
    }

    Type t_ = Type::Null;
    bool b_ = false;
    double n_ = 0;
    std::string s_;
    std::vector<Json> items_;       // array elements, or object values
    std::vector<std::string> keys_; // object keys (parallel to items_)
};

struct JsonParser {
    explicit JsonParser(std::string_view text) : s(text) {}
    std::string_view s;
    size_t i = 0;
    std::string err;

    bool fail(const char* msg) {
        if (err.empty()) {
            int line = 1;
            for (size_t k = 0; k < i && k < s.size(); k++) if (s[k] == '\n') line++;
            err = std::string(msg) + " (line " + std::to_string(line) + ")";
        }
        return false;
    }
    void ws() {
        while (i < s.size()) {
            if (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
            else if (s.substr(i, 2) == "//") { while (i < s.size() && s[i] != '\n') i++; }
            else break;
        }
    }
    bool lit(std::string_view w) {
        if (s.substr(i, w.size()) != w) return false;
        i += w.size();
        return true;
    }
    bool str(std::string& out) {
        i++; // opening quote
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) return fail("bad escape");
            char e = s[i++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (i + 4 > s.size()) return fail("bad \\u escape");
                    unsigned cp = (unsigned)std::strtoul(std::string(s.substr(i, 4)).c_str(), nullptr, 16);
                    i += 4;
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) { out += (char)(0xC0 | cp >> 6); out += (char)(0x80 | (cp & 0x3F)); }
                    else { out += (char)(0xE0 | cp >> 12); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                    break;
                }
                default: out += e; // \" \\ \/
            }
        }
        if (i >= s.size()) return fail("unterminated string");
        i++; // closing quote
        return true;
    }
    bool value(Json& j, int depth = 0) {
        if (depth > 64) return fail("nesting too deep");
        ws();
        if (i >= s.size()) return fail("unexpected end");
        char c = s[i];
        if (c == '{') {
            j.t_ = Json::Type::Object;
            i++; ws();
            if (i < s.size() && s[i] == '}') { i++; return true; }
            while (true) {
                ws();
                if (i >= s.size() || s[i] != '"') return fail("expected key string");
                std::string k;
                if (!str(k)) return false;
                ws();
                if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                i++;
                Json v;
                if (!value(v, depth + 1)) return false;
                j.keys_.push_back(std::move(k));
                j.items_.push_back(std::move(v));
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == '}') { i++; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            j.t_ = Json::Type::Array;
            i++; ws();
            if (i < s.size() && s[i] == ']') { i++; return true; }
            while (true) {
                Json v;
                if (!value(v, depth + 1)) return false;
                j.items_.push_back(std::move(v));
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == ']') { i++; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { j.t_ = Json::Type::String; return str(j.s_); }
        if (lit("true"))  { j.t_ = Json::Type::Bool; j.b_ = true; return true; }
        if (lit("false")) { j.t_ = Json::Type::Bool; j.b_ = false; return true; }
        if (lit("null"))  { return true; }
        char* end = nullptr;
        std::string tmp(s.substr(i, 32));
        double d = std::strtod(tmp.c_str(), &end);
        if (end == tmp.c_str()) return fail("unexpected character");
        i += (size_t)(end - tmp.c_str());
        j.t_ = Json::Type::Number;
        j.n_ = d;
        return true;
    }
};

inline Json Json::parse(std::string_view text, std::string* error) {
    JsonParser p(text);
    Json j;
    bool ok = p.value(j);
    if (ok) { p.ws(); if (p.i < text.size()) { p.fail("trailing characters"); ok = false; } }
    if (!ok) { if (error) *error = p.err; return Json{}; }
    return j;
}

} // namespace engine
