#pragma once
// Pure OBJ + MTL parser: no SDL, no GL, no files needed (text in, data out), so it is unit-tested (tests/test_obj_parser.cpp).
// core/import_handler wraps it as the ".obj" loader; anything else may use it directly.
//
//   * faces: v, v/vt, v//vn, v/vt/vn, negative (relative) indices; quads and n-gons are fan-triangulated
//   * missing normals: flat face normal per triangle
//   * colours: per-vertex RGBA from the Kd / d of the material named by usemtl, in the .mtl named by mtllib.
//     No MTL / unknown material => a neutral grey. A material called CANOPY with d=1 becomes 30% alpha (assets/models/README.md).
//   * CUSTOM '@' MATERIALS: a material whose name STARTS with '@' is not ordinary geometry. Its faces are pulled out as a
//     TaggedQuad (never added to the triangle list) and handed to whoever draws custom content for that tag.
//     Tag format "@GROUP-NAME": GROUP is the text before the first '-', NAME the rest ("@HUD-INFO" => HUD / INFO).
//     No dash => the whole text is the group and the name is empty. An '@' anywhere but the first character is not a tag.
#include <functional>
#include <string>
#include <vector>
#include "engine/math.h"

namespace core {

struct TaggedQuad {
    std::string tag;                  // the material name as written, e.g. "@HUD-INFO"
    std::string group;                // "HUD"
    std::string name;                 // "INFO" ("" when the tag has no dash)
    engine::Vec3 corners[4];          // as the modeller wrote them: bottom-left, bottom-right, top-right, top-left
    engine::Vec3 normal;              // unit, = (c1 - c0) x (c3 - c0): points at the viewer when the corners run counter-clockwise
    engine::Vec3 centre;
    float width = 0, height = 0;      // average of opposite edges, model units
    float color[4] = {0.8f, 0.8f, 0.8f, 1.0f};   // the tag material's own Kd/d (grey if the .mtl has no entry for it), for a tag whose faces
                                                   // should ALSO be drawn as ordinary geometry (e.g. @THRUST-JET: a marker AND a visible nozzle)

    void compute();
    // Point on the quad: u 0..1 left -> right, v 0..1 top -> bottom (screen convention).
    engine::Vec3 at(float u, float v) const;
};

// Tag helpers (exposed for tests and for modules that match ship.json tags).
bool isTagMaterial(const std::string& material);                                      // starts with '@'
void splitTag(const std::string& material, std::string& group, std::string& name);   // "@HUD-INFO" -> "HUD", "INFO"

struct ObjParseResult {
    std::vector<float> positions;     // xyz per vertex, flat triangle list
    std::vector<float> normals;       // xyz per vertex
    std::vector<float> colors;        // rgba per vertex
    std::vector<TaggedQuad> tagged;   // faces of '@' materials, excluded from the arrays above
    std::vector<std::string> warnings;
    size_t triangleCount() const { return positions.size() / 9; }
};

// Supplies the text of a material library by the name written after mtllib. Return false if it cannot be read.
using MtlReader = std::function<bool(const std::string& name, std::string& text)>;

// Never throws; malformed lines are skipped (and counted in warnings). An empty/garbage text yields an empty result.
ObjParseResult parseObj(const std::string& objText, const MtlReader& readMtl = {});

// Reads 'path' and resolves mtllib next to it. False only if the OBJ file cannot be opened.
bool parseObjFile(const std::string& path, ObjParseResult& out);

} // namespace core
