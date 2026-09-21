#pragma once
// Pure geometry (no GL, unit-tested in tests/test_thrusters.cpp): turn the '@THRUST-JET' tagged faces of a ship model into engine exhaust emitters.
//
// The modeller paints the spot where the exhaust comes out with the material @THRUST-JET. The OBJ parser turns tagged faces into TaggedQuads (a
// triangle completed to a parallelogram, n-gons cut to four corners), which loses the real shape, so the jet faces are read straight from the OBJ
// text here (jetPolygonsFromObj: triangles, quads and n-gons exactly as written). Faces that touch or lie close together (clusterDist) form ONE
// emitter; two engines far apart are two emitters.
//   position  = area-weighted centre of the cluster (ship model space: eye at the origin, +x right, +y up, -z forward, metres)
//   direction = area-weighted face normal from the vertex winding (counter-clockwise seen from outside = toward the viewer), flipped if it points
//               toward the hull centre (the exhaust always leaves the body)
//   radius    = farthest vertex from the centre
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>
#include "engine/math.h"

namespace cockpit {

struct ThrusterJet {
    engine::Vec3 position;    // ship frame
    engine::Vec3 direction;   // ship frame, unit: where the exhaust goes
    float radius = 0;
};

using JetPolygon = std::vector<engine::Vec3>;

// The faces drawn with material 'tag' in an OBJ text, as polygons (v, v/vt, v//vn, v/vt/vn, negative indices). Bad indices are skipped.
inline std::vector<JetPolygon> jetPolygonsFromObj(const std::string& text, const std::string& tag = "@THRUST-JET") {
    std::vector<engine::Vec3> v;
    std::vector<JetPolygon> out;
    std::istringstream in(text);
    std::string line, kw;
    bool cur = false;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        if (!(ss >> kw)) continue;
        if (kw == "v") { engine::Vec3 p{0, 0, 0}; ss >> p.x >> p.y >> p.z; v.push_back(p); }
        else if (kw == "usemtl") { std::string m; ss >> m; cur = m == tag; }
        else if (kw == "f" && cur) {
            JetPolygon poly;
            std::string tok;
            bool ok = true;
            while (ss >> tok) {
                long i = std::strtol(tok.c_str(), nullptr, 10);
                if (i < 0) i += (long)v.size() + 1;
                if (i < 1 || i > (long)v.size()) { ok = false; break; }
                poly.push_back(v[(size_t)i - 1]);
            }
            if (ok && poly.size() >= 3) out.push_back(std::move(poly));
        }
    }
    return out;
}

// hullCentre: a point inside the body (e.g. the model's bounding-box centre), used to orient the normals outward.
inline std::vector<ThrusterJet> thrusterJets(const std::vector<JetPolygon>& polys, const engine::Vec3& hullCentre, float clusterDist = 0.5f) {
    struct Face { JetPolygon v; engine::Vec3 centre, areaNormal; float area; };
    std::vector<Face> faces;
    for (const auto& poly : polys) {
        Face f{};
        f.v = poly;
        engine::Vec3 an{0, 0, 0}, c{0, 0, 0};
        float area = 0;
        for (size_t i = 1; i + 1 < f.v.size(); i++) {   // fan triangles, area-weighted centroid and normal
            engine::Vec3 cr = engine::cross(f.v[i] - f.v[0], f.v[i + 1] - f.v[0]);
            float a = 0.5f * engine::length(cr);
            an = an + cr * 0.5f;
            c = c + (f.v[0] + f.v[i] + f.v[i + 1]) * (a / 3.0f);
            area += a;
        }
        if (!(area > 1e-9f)) continue;               // degenerate: no area, no direction
        f.centre = c * (1.0f / area);
        f.areaNormal = an;
        f.area = area;
        faces.push_back(std::move(f));
    }
    // single-link clustering: faces sharing a vertex or with centres / vertices closer than clusterDist
    const size_t n = faces.size();
    std::vector<size_t> parent(n);
    for (size_t i = 0; i < n; i++) parent[i] = i;
    auto root = [&](size_t i) { while (parent[i] != i) i = parent[i] = parent[parent[i]]; return i; };
    auto close = [&](const Face& a, const Face& b) {
        for (size_t i = 0; i < a.v.size(); i++)
            for (size_t j = 0; j < b.v.size(); j++)
                if (engine::length(a.v[i] - b.v[j]) <= clusterDist) return true;
        return engine::length(a.centre - b.centre) <= clusterDist;
    };
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++)
            if (close(faces[i], faces[j])) parent[root(i)] = root(j);

    std::vector<ThrusterJet> jets;
    std::vector<size_t> roots;
    for (size_t i = 0; i < n; i++) {
        size_t r = root(i);
        bool seen = false;
        for (size_t k : roots) if (k == r) seen = true;
        if (seen) continue;
        roots.push_back(r);
        engine::Vec3 c{0, 0, 0}, an{0, 0, 0};
        float area = 0;
        for (size_t j = 0; j < n; j++)
            if (root(j) == r) { c = c + faces[j].centre * faces[j].area; an = an + faces[j].areaNormal; area += faces[j].area; }
        ThrusterJet jet;
        jet.position = c * (1.0f / area);
        float l = engine::length(an);
        if (!(l > 1e-9f)) continue;                   // opposite faces cancel out: no usable direction
        jet.direction = an * (1.0f / l);
        engine::Vec3 out = jet.position - hullCentre;
        if (engine::dot(jet.direction, out) < 0) jet.direction = jet.direction * -1.0f;
        for (size_t j = 0; j < n; j++)
            if (root(j) == r)
                for (size_t k = 0; k < faces[j].v.size(); k++) jet.radius = std::max(jet.radius, engine::length(faces[j].v[k] - jet.position));
        jets.push_back(jet);
    }
    return jets;
}

} // namespace cockpit
