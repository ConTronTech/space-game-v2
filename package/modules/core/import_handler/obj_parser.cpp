#include "core/import_handler/obj_parser.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace core {

using engine::Vec3;

// ---- tags ----

bool isTagMaterial(const std::string& m) { return !m.empty() && m[0] == '@'; }

void splitTag(const std::string& material, std::string& group, std::string& name) {
    std::string body = isTagMaterial(material) ? material.substr(1) : material;
    size_t dash = body.find('-');
    group = body.substr(0, dash);
    name = dash == std::string::npos ? "" : body.substr(dash + 1);
}

void TaggedQuad::compute() {
    centre = (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;
    normal = engine::normalize(engine::cross(corners[1] - corners[0], corners[3] - corners[0]));
    width = (engine::length(corners[1] - corners[0]) + engine::length(corners[2] - corners[3])) * 0.5f;
    height = (engine::length(corners[3] - corners[0]) + engine::length(corners[2] - corners[1])) * 0.5f;
}

Vec3 TaggedQuad::at(float u, float v) const {
    Vec3 top = corners[3] + (corners[2] - corners[3]) * u;
    Vec3 bottom = corners[0] + (corners[1] - corners[0]) * u;
    return top + (bottom - top) * v;
}

// ---- helpers ----
namespace {

struct Material { float r = 0.8f, g = 0.8f, b = 0.8f, a = 1.0f; };
constexpr Material kNeutral{};

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

// Text after the keyword on a line, trimmed.
std::string rest(const std::string& line, size_t keywordLen) { return trim(line.substr(std::min(keywordLen, line.size()))); }

bool startsWithKeyword(const std::string& line, const char* kw) {
    size_t n = std::char_traits<char>::length(kw);
    return line.size() > n && line.compare(0, n, kw) == 0 && (line[n] == ' ' || line[n] == '\t');
}

void parseMtl(const std::string& text, std::map<std::string, Material>& out) {
    std::istringstream in(text);
    std::string line;
    Material* cur = nullptr;
    while (std::getline(in, line)) {
        line = trim(line);
        if (startsWithKeyword(line, "newmtl")) {
            cur = &(out[rest(line, 6)] = Material{});
        } else if (cur && startsWithKeyword(line, "Kd")) {
            std::istringstream ss(rest(line, 2));
            float r, g, b;
            if (ss >> r >> g >> b) { cur->r = r; cur->g = g; cur->b = b; }
        } else if (cur && startsWithKeyword(line, "d")) {
            std::istringstream ss(rest(line, 1));
            float a;
            if (ss >> a) cur->a = std::clamp(a, 0.0f, 1.0f);
        } else if (cur && startsWithKeyword(line, "Tr")) {   // Tr = 1 - d
            std::istringstream ss(rest(line, 2));
            float t;
            if (ss >> t) cur->a = std::clamp(1.0f - t, 0.0f, 1.0f);
        }
    }
}

struct FaceVert { long v = 0, n = 0; bool ok = true; };

// "v", "v/vt", "v//vn", "v/vt/vn" -> resolved 1-based indices (negative = relative to what exists now)
FaceVert parseFaceVert(const std::string& tok, long nPos, long nNorm) {
    FaceVert f;
    std::string parts[3];
    size_t start = 0;
    for (int i = 0; i < 3; i++) {
        size_t slash = tok.find('/', start);
        parts[i] = tok.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    auto num = [](const std::string& s, long& out) {
        if (s.empty()) return true;                                   // absent component
        char* end = nullptr;
        out = std::strtol(s.c_str(), &end, 10);
        return end && *end == '\0';
    };
    if (parts[0].empty() || !num(parts[0], f.v)) f.ok = false;
    long vt = 0;
    if (!num(parts[1], vt)) f.ok = false;
    if (!num(parts[2], f.n)) f.ok = false;
    if (f.v < 0) f.v += nPos + 1;
    if (f.n < 0) f.n += nNorm + 1;
    if (f.v < 1 || f.v > nPos) f.ok = false;
    if (f.n < 0 || f.n > nNorm) f.n = 0;                             // bad normal index: fall back to the face normal
    return f;
}

} // namespace

ObjParseResult parseObj(const std::string& text, const MtlReader& readMtl) {
    ObjParseResult out;
    std::vector<Vec3> v, vn;
    std::map<std::string, Material> mats;
    Material cur = kNeutral;
    std::string curName;
    bool curTagged = false;
    std::string curGroup, curTagName;
    int badFaces = 0, badTagged = 0;
    std::map<std::string, bool> warnedMaterial;

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        if (startsWithKeyword(line, "v")) {
            std::istringstream ss(rest(line, 1));
            Vec3 p;
            if (ss >> p.x >> p.y >> p.z) v.push_back(p);
        } else if (startsWithKeyword(line, "vn")) {
            std::istringstream ss(rest(line, 2));
            Vec3 n;
            if (ss >> n.x >> n.y >> n.z) vn.push_back(n);
        } else if (startsWithKeyword(line, "mtllib")) {
            std::string name = rest(line, 6), body;
            if (!readMtl || !readMtl(name, body)) out.warnings.push_back("material library not found: " + name + " (using grey)");
            else parseMtl(body, mats);
        } else if (startsWithKeyword(line, "usemtl")) {
            curName = rest(line, 6);
            curTagged = isTagMaterial(curName);
            if (curTagged) splitTag(curName, curGroup, curTagName);
            auto it = mats.find(curName);
            if (it != mats.end()) {
                cur = it->second;
                if (curName == "CANOPY" && cur.a >= 0.99f) cur.a = 0.3f;
            } else {
                cur = kNeutral;
                if (!curTagged && !warnedMaterial[curName]) {   // tagged materials need no MTL entry
                    warnedMaterial[curName] = true;
                    out.warnings.push_back("material '" + curName + "' not defined (using grey)");
                }
            }
        } else if (startsWithKeyword(line, "f")) {
            std::istringstream ss(rest(line, 1));
            std::vector<FaceVert> face;
            std::string tok;
            bool ok = true;
            while (ss >> tok) {
                face.push_back(parseFaceVert(tok, (long)v.size(), (long)vn.size()));
                if (!face.back().ok) ok = false;
            }
            if (face.size() < 3 || !ok) { (curTagged ? badTagged : badFaces)++; continue; }

            if (curTagged) {
                TaggedQuad q;
                q.tag = curName; q.group = curGroup; q.name = curTagName;
                q.color[0] = cur.r; q.color[1] = cur.g; q.color[2] = cur.b; q.color[3] = cur.a;
                if (face.size() == 3) {         // a screen needs four corners: complete the parallelogram a + c - b
                    q.corners[0] = v[(size_t)face[0].v - 1];
                    q.corners[1] = v[(size_t)face[1].v - 1];
                    q.corners[2] = v[(size_t)face[2].v - 1];
                    q.corners[3] = q.corners[0] + q.corners[2] - q.corners[1];
                    out.warnings.push_back("tagged face of " + curName + " is a triangle: completed to a parallelogram");
                } else {
                    for (int i = 0; i < 4; i++) q.corners[i] = v[(size_t)face[(size_t)i].v - 1];
                    if (face.size() > 4) out.warnings.push_back("tagged face of " + curName + " has " + std::to_string(face.size()) + " vertices: using the first four");
                }
                q.compute();
                out.tagged.push_back(std::move(q));
                continue;
            }

            for (size_t k = 1; k + 1 < face.size(); k++) {   // fan-triangulate
                const FaceVert tri[3] = {face[0], face[k], face[k + 1]};
                Vec3 fn = engine::normalize(engine::cross(v[(size_t)tri[1].v - 1] - v[(size_t)tri[0].v - 1],
                                                          v[(size_t)tri[2].v - 1] - v[(size_t)tri[0].v - 1]));
                if (engine::length(fn) < 0.5f) fn = {0, 1, 0};   // degenerate triangle: any unit vector keeps lighting sane
                for (const FaceVert& t : tri) {
                    Vec3 p = v[(size_t)t.v - 1];
                    Vec3 n = t.n >= 1 ? vn[(size_t)t.n - 1] : fn;
                    out.positions.insert(out.positions.end(), {p.x, p.y, p.z});
                    out.normals.insert(out.normals.end(), {n.x, n.y, n.z});
                    out.colors.insert(out.colors.end(), {cur.r, cur.g, cur.b, cur.a});
                }
            }
        }
    }
    if (badFaces) out.warnings.push_back(std::to_string(badFaces) + " face(s) skipped: bad or missing vertex index");
    if (badTagged) out.warnings.push_back(std::to_string(badTagged) + " tagged face(s) skipped: bad or missing vertex index");
    return out;
}

bool parseObjFile(const std::string& path, ObjParseResult& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    size_t slash = path.find_last_of('/');
    std::string dir = slash == std::string::npos ? "" : path.substr(0, slash + 1);
    out = parseObj(ss.str(), [&dir](const std::string& name, std::string& body) {
        std::ifstream m(dir + name);
        if (!m) return false;
        std::stringstream ms;
        ms << m.rdbuf();
        body = ms.str();
        return true;
    });
    return true;
}

} // namespace core
