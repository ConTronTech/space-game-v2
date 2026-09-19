#pragma once
// Tunables: every value has a base default in code; config/game.json may override it.
//
//     float thrust = eng.config.get("flight.thrust", 40.0f, "forward acceleration, m/s^2");
//
// - Key missing, or set to the string "DEFAULT"  => the value in code is used.
// - Set it to your own number/bool/string        => that is used instead.
// The game keeps config/game.json up to date: every key any module asks for is added as "DEFAULT"
// with a comment showing the real default, and your overrides are preserved. Read at startup.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "engine/json.h"
#include "engine/log.h"

namespace engine {

class Config {
public:
    explicit Config(std::string path = "config/game.json") : path_(std::move(path)) {}

    void load() {
        std::ifstream f(path_);
        if (!f) return;
        std::stringstream ss;
        ss << f.rdbuf();
        std::string err;
        Json j = Json::parse(ss.str(), &err);
        if (!j.isObject()) { LOG_W("config", "%s: %s - using defaults", path_.c_str(), err.empty() ? "expected an object" : err.c_str()); return; }
        fromJson(j, root_);
    }

    template <class T>
    T get(const std::string& key, T def, const std::string& doc = "") {
        static_assert(std::is_arithmetic_v<T> || std::is_same_v<T, std::string>, "config values: number, bool or string");
        T out = def;
        Node* n = find(key);
        if (n && n->leaf) {
            if (n->type == Node::Str && n->text == "DEFAULT") {
                // explicitly default
            } else if constexpr (std::is_same_v<T, bool>) {
                if (n->type == Node::Bool) out = n->text == "true"; else warn(key, "a true/false");
            } else if constexpr (std::is_arithmetic_v<T>) {
                if (n->type == Node::Num) out = (T)std::strtod(n->text.c_str(), nullptr); else warn(key, "a number");
            } else {
                if (n->type == Node::Str) out = n->text; else warn(key, "a string");
            }
        } else if (n) {
            warn(key, "a single value");
        }
        record(key, literal(def), doc);
        return out;
    }

    // Writes config/game.json if any tunable was added this run (creates the file the first time).
    void save() {
        if (!dirty_) return;
        std::ofstream f(path_);
        if (!f) { LOG_E("config", "cannot write %s", path_.c_str()); return; }
        f << "// Tunables. Every value shows \"DEFAULT\" (use the built-in value shown in the comment).\n"
             "// Replace \"DEFAULT\" with your own number/true/false to change it. Applied on next launch.\n";
        write(f, root_, 0);
        f << "\n";
        dirty_ = false;
    }

private:
    struct Node {
        enum Type { Num, Bool, Str } type = Str;
        bool leaf = false;
        std::string text, comment;
        std::map<std::string, Node> kids;
    };

    static std::string fmtNum(double d, int digits = 10) {
        char b[48];
        std::snprintf(b, sizeof b, "%.*g", digits, d);
        std::string s = b;
        if (s.find_first_of(".e") == std::string::npos) s += ".0";
        return s;
    }
    template <class T> static std::string literal(const T& v) {
        if constexpr (std::is_same_v<T, bool>) return v ? "true" : "false";
        else if constexpr (std::is_same_v<T, float>) return fmtNum((double)v, 7); // avoid 0.3 -> 0.3000000119
        else if constexpr (std::is_arithmetic_v<T>) return fmtNum((double)v);
        else return v;
    }

    static void split(const std::string& key, std::vector<std::string>& parts) {
        size_t i = 0;
        while (i <= key.size()) {
            size_t j = key.find('.', i);
            if (j == std::string::npos) j = key.size();
            parts.push_back(key.substr(i, j - i));
            i = j + 1;
        }
    }
    Node* find(const std::string& key) {
        std::vector<std::string> parts;
        split(key, parts);
        Node* n = &root_;
        for (auto& p : parts) {
            auto it = n->kids.find(p);
            if (it == n->kids.end()) return nullptr;
            n = &it->second;
        }
        return n;
    }
    void record(const std::string& key, const std::string& def, const std::string& doc) {
        std::vector<std::string> parts;
        split(key, parts);
        Node* n = &root_;
        for (auto& p : parts) {
            if (n->leaf) return; // clashes with an existing plain value; ignore
            n = &n->kids[p];
        }
        if (!n->leaf && !n->kids.empty()) return;
        if (!n->leaf) { n->leaf = true; n->type = Node::Str; n->text = "DEFAULT"; dirty_ = true; }
        std::string c = "default " + def;
        if (!doc.empty()) c += " - " + doc;
        n->comment = c;
    }
    void warn(const std::string& key, const char* want) {
        if (warned_.insert(key).second)
            LOG_W("config", "%s in %s should be %s (or \"DEFAULT\") - using default", key.c_str(), path_.c_str(), want);
    }
    static void fromJson(const Json& j, Node& n) {
        for (auto& k : j.keys()) {
            const Json& v = j[k];
            Node& c = n.kids[k];
            if (v.isObject()) { fromJson(v, c); continue; }
            if (v.type() == Json::Type::Number)      { c.leaf = true; c.type = Node::Num;  c.text = fmtNum(v.num()); }
            else if (v.type() == Json::Type::Bool)   { c.leaf = true; c.type = Node::Bool; c.text = v.boolean() ? "true" : "false"; }
            else if (v.type() == Json::Type::String) { c.leaf = true; c.type = Node::Str;  c.text = v.str(); }
            else n.kids.erase(k); // arrays/null: not a tunable
        }
    }
    static std::string quote(const std::string& s) {
        std::string o = "\"";
        for (char ch : s) { if (ch == '"' || ch == '\\') o += '\\'; o += ch; }
        return o + "\"";
    }
    static void write(std::ostream& f, const Node& n, int depth) {
        std::string pad((size_t)(depth + 1) * 2, ' ');
        f << "{\n";
        size_t i = 0;
        for (auto& [k, c] : n.kids) {
            bool last = ++i == n.kids.size();
            f << pad << quote(k) << ": ";
            if (c.leaf) {
                std::string v = c.type == Node::Str ? quote(c.text) : c.text;
                f << v << (last ? "" : ",");
                if (!c.comment.empty()) f << "   // " << c.comment;
            } else {
                write(f, c, depth + 1);
                if (!last) f << ",";
            }
            f << "\n";
        }
        f << std::string((size_t)depth * 2, ' ') << "}";
    }

    std::string path_;
    Node root_;
    bool dirty_ = false;
    std::set<std::string> warned_;
};

} // namespace engine
