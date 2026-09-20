#include <cmath>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include "core/import_handler/obj_parser.h"
#include "tests/test.h"

namespace {

// Writes files into a private temp folder that is removed again.
struct TempDir {
    std::filesystem::path dir;
    TempDir() {
        static int n = 0;
        dir = std::filesystem::temp_directory_path() / ("sg2_objtest_" + std::to_string(::getpid()) + "_" + std::to_string(n++));
        std::filesystem::create_directories(dir);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
    std::string write(const std::string& name, const std::string& text) const {
        std::ofstream(dir / name) << text;
        return (dir / name).string();
    }
};

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
bool hasWarning(const core::ObjParseResult& r, const std::string& needle) {
    for (auto& w : r.warnings) if (w.find(needle) != std::string::npos) return true;
    return false;
}

const char* kSquare = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n";

} // namespace

TEST(obj_triangle_with_normals_v_vt_vn) {
    TempDir t;
    auto p = t.write("a.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n");
    core::ObjParseResult r;
    CHECK(core::parseObjFile(p, r));
    CHECK_EQ(r.triangleCount(), (size_t)1);
    CHECK_EQ(r.positions.size(), (size_t)9);
    CHECK_EQ(r.normals.size(), (size_t)9);
    CHECK_EQ(r.colors.size(), (size_t)12);
    CHECK(near(r.normals[2], 1.0f));
    CHECK(near(r.positions[3], 1.0f));   // second vertex x
}

TEST(obj_double_slash_normals) {
    auto r = core::parseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 1 0\nf 1//1 2//1 3//1\n");
    CHECK_EQ(r.triangleCount(), (size_t)1);
    CHECK(near(r.normals[1], 1.0f));     // the file's normal, not the computed +Z one
    CHECK(near(r.normals[2], 0.0f));
}

TEST(obj_plain_indices_compute_normals) {
    auto r = core::parseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    CHECK_EQ(r.triangleCount(), (size_t)1);
    for (int i = 0; i < 3; i++) {
        CHECK(near(r.normals[(size_t)i * 3 + 0], 0.0f));
        CHECK(near(r.normals[(size_t)i * 3 + 2], 1.0f));   // counter-clockwise triangle in the XY plane faces +Z
    }
}

TEST(obj_quad_fan_triangulated) {
    auto r = core::parseObj(std::string(kSquare) + "f 1 2 3 4\n");
    CHECK_EQ(r.triangleCount(), (size_t)2);
    // fan: (1,2,3) and (1,3,4)
    CHECK(near(r.positions[6], 1.0f) && near(r.positions[7], 1.0f));      // tri 0, vertex 2 = (1,1)
    CHECK(near(r.positions[9], 0.0f) && near(r.positions[10], 0.0f));     // tri 1 starts at vertex 1
    CHECK(near(r.positions[15], 0.0f) && near(r.positions[16], 1.0f));    // tri 1, vertex 2 = (0,1)
}

TEST(obj_ngon_fan_triangulated) {
    auto r = core::parseObj("v 0 0 0\nv 1 0 0\nv 2 1 0\nv 1 2 0\nv 0 1 0\nf 1 2 3 4 5\n");
    CHECK_EQ(r.triangleCount(), (size_t)3);
    CHECK_EQ(r.positions.size(), (size_t)27);
}

TEST(obj_negative_indices_are_relative) {
    // relative to the vertices that exist when the face is read: -1 = the last one
    auto r = core::parseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\nv 5 5 5\nf -1 -2 -3\n");
    CHECK_EQ(r.triangleCount(), (size_t)2);
    CHECK(near(r.positions[0], 0.0f) && near(r.positions[3], 1.0f));      // first face: v1 v2 v3
    CHECK(near(r.positions[9], 5.0f));                                     // second face starts at the vertex added afterwards
}

TEST(obj_negative_normal_index) {
    auto r = core::parseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 1 0 0\nvn 0 0 1\nf 1//-1 2//-1 3//-1\n");
    CHECK_EQ(r.triangleCount(), (size_t)1);
    CHECK(near(r.normals[2], 1.0f));     // -1 = the second normal
}

TEST(obj_bad_indices_skip_face_not_crash) {
    auto r = core::parseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 9\nf 0 1 2\nf 1 2 x\nf -9 1 2\nf 1 2\nf 1 2 3\n");
    CHECK_EQ(r.triangleCount(), (size_t)1);            // only the last face is valid
    CHECK(hasWarning(r, "skipped"));
}

TEST(obj_bad_normal_index_falls_back_to_face_normal) {
    auto r = core::parseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 1 0\nf 1//7 2//7 3//7\n");
    CHECK_EQ(r.triangleCount(), (size_t)1);
    CHECK(near(r.normals[2], 1.0f));
}

TEST(obj_empty_and_garbage_inputs) {
    CHECK_EQ(core::parseObj("").triangleCount(), (size_t)0);
    CHECK_EQ(core::parseObj("\n\n# only a comment\n").triangleCount(), (size_t)0);
    CHECK_EQ(core::parseObj("v a b c\nf 1 2 3\nusemtl\nmtllib\n\x01\x02 zzz\n").triangleCount(), (size_t)0);
    core::ObjParseResult r;
    TempDir t;
    CHECK(core::parseObjFile(t.write("empty.obj", ""), r));
    CHECK_EQ(r.triangleCount(), (size_t)0);
    CHECK(!core::parseObjFile((t.dir / "missing.obj").string(), r));
}

TEST(obj_crlf_line_endings) {
    auto r = core::parseObj("v 0 0 0\r\nv 1 0 0\r\nv 0 1 0\r\nf 1 2 3\r\n");
    CHECK_EQ(r.triangleCount(), (size_t)1);
}

TEST(obj_mtl_colors_per_material) {
    TempDir t;
    t.write("s.mtl", "newmtl RED\nKd 1 0 0\nd 1\n\nnewmtl GLASS\nKd 0 0.5 1\nd 0.25\nnewmtl CANOPY\nKd 0 0 1\nd 1\n");
    auto p = t.write("s.obj", std::string("mtllib s.mtl\n") + kSquare +
                                  "usemtl RED\nf 1 2 3\nusemtl GLASS\nf 1 3 4\nusemtl CANOPY\nf 1 2 4\n");
    core::ObjParseResult r;
    CHECK(core::parseObjFile(p, r));
    CHECK_EQ(r.triangleCount(), (size_t)3);
    CHECK(near(r.colors[0], 1.0f) && near(r.colors[1], 0.0f) && near(r.colors[3], 1.0f));      // RED, opaque
    CHECK(near(r.colors[12 + 1], 0.5f) && near(r.colors[12 + 3], 0.25f));                       // GLASS keeps d
    CHECK(near(r.colors[24 + 2], 1.0f) && near(r.colors[24 + 3], 0.3f));                        // CANOPY forced to 30%
    CHECK(r.warnings.empty());
}

TEST(obj_missing_mtl_gives_neutral_grey) {
    TempDir t;
    auto p = t.write("m.obj", std::string("mtllib nope.mtl\n") + kSquare + "usemtl X\nf 1 2 3\n");
    core::ObjParseResult r;
    CHECK(core::parseObjFile(p, r));
    CHECK_EQ(r.triangleCount(), (size_t)1);
    CHECK(near(r.colors[0], r.colors[1]) && near(r.colors[1], r.colors[2]));   // grey: r == g == b
    CHECK(r.colors[0] > 0.2f && r.colors[0] < 0.95f);
    CHECK(near(r.colors[3], 1.0f));
    CHECK(hasWarning(r, "nope.mtl"));
}

TEST(obj_unknown_material_and_no_usemtl_are_grey) {
    auto r = core::parseObj(std::string(kSquare) + "f 1 2 3\nusemtl GHOST\nf 1 3 4\n", [](const std::string&, std::string&) { return false; });
    CHECK_EQ(r.triangleCount(), (size_t)2);
    CHECK(near(r.colors[0], r.colors[12]));   // both grey and the same grey
    CHECK(hasWarning(r, "GHOST"));
}

// ---------- '@' custom materials (modelled on assets/models/ship/shipv2/ShipV2.mtl) ----------

namespace {
const char* kShipMtl =
    "newmtl @HUD-INFO\nKd 0 0.9 0\nnewmtl @HUD-SYSTEMS\nKd 0 0 0.8\nnewmtl @HUD-RADAR\nKd 0.8 0 0\n"
    "newmtl BODY\nKd 0.1 0.02 0.1\nnewmtl NOTA@TAG\nKd 1 1 1\n";
core::MtlReader mtl(const char* text) {
    return [text](const std::string&, std::string& out) { out = text; return true; };
}
}

TEST(obj_tagged_quad_excluded_from_triangles) {
    // a 2 x 1 quad in the XZ plane facing +Y: corners bottom-left, bottom-right, top-right, top-left as ShipV2 writes them
    std::string obj =
        "mtllib s.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"           // 1..4: an ordinary square
        "v 0 0 0\nv 2 0 0\nv 2 0 -1\nv 0 0 -1\n"                        // 5..8: the screen
        "usemtl BODY\nf 1 2 3 4\nusemtl @HUD-INFO\nf 5 6 7 8\nusemtl BODY\nf 1 2 3\n";
    auto r = core::parseObj(obj, mtl(kShipMtl));
    CHECK_EQ(r.triangleCount(), (size_t)3);          // 2 from the quad + 1: the screen added none
    CHECK_EQ(r.tagged.size(), (size_t)1);
    const auto& q = r.tagged[0];
    CHECK_EQ(q.tag, std::string("@HUD-INFO"));
    CHECK_EQ(q.group, std::string("HUD"));
    CHECK_EQ(q.name, std::string("INFO"));
    CHECK(near(q.centre.x, 1.0f) && near(q.centre.y, 0.0f) && near(q.centre.z, -0.5f));
    CHECK(near(q.width, 2.0f) && near(q.height, 1.0f));
    // (2,0,0)-(0,0,0) = +X, (0,0,-1)-(0,0,0) = -Z ; +X cross -Z = +Y
    CHECK(near(q.normal.x, 0.0f) && near(q.normal.y, 1.0f) && near(q.normal.z, 0.0f));
    auto tl = q.at(0, 0), br = q.at(1, 1);
    CHECK(near(tl.z, -1.0f) && near(tl.x, 0.0f));    // u=0,v=0 is corner 3 (top-left)
    CHECK(near(br.x, 2.0f) && near(br.z, 0.0f));     // u=1,v=1 is corner 1 (bottom-right)
}

TEST(obj_several_tagged_quads_keep_their_names) {
    std::string obj = std::string("mtllib s.mtl\n") + kSquare +
        "usemtl @HUD-INFO\nf 1 2 3 4\nusemtl @HUD-SYSTEMS\nf 1 2 3 4\nusemtl @HUD-RADAR\nf 1 2 3 4\n"
        "usemtl @HUD-RADAR\nf 4 3 2 1\n";
    auto r = core::parseObj(obj, mtl(kShipMtl));
    CHECK_EQ(r.triangleCount(), (size_t)0);
    CHECK_EQ(r.tagged.size(), (size_t)4);
    CHECK_EQ(r.tagged[0].name, std::string("INFO"));
    CHECK_EQ(r.tagged[1].name, std::string("SYSTEMS"));
    CHECK_EQ(r.tagged[2].name, std::string("RADAR"));
    CHECK_EQ(r.tagged[3].name, std::string("RADAR"));
    for (auto& q : r.tagged) CHECK_EQ(q.group, std::string("HUD"));
}

TEST(obj_other_groups_work_the_same) {
    auto r = core::parseObj(std::string(kSquare) + "usemtl @SCREEN-MAP-LEFT\nf 1 2 3 4\n", mtl(kShipMtl));
    CHECK_EQ(r.tagged.size(), (size_t)1);
    CHECK_EQ(r.tagged[0].group, std::string("SCREEN"));
    CHECK_EQ(r.tagged[0].name, std::string("MAP-LEFT"));     // only the first dash splits
}

TEST(obj_tag_without_dash) {
    auto r = core::parseObj(std::string(kSquare) + "usemtl @GLOW\nf 1 2 3 4\n", mtl(kShipMtl));
    CHECK_EQ(r.tagged.size(), (size_t)1);
    CHECK_EQ(r.tagged[0].group, std::string("GLOW"));
    CHECK_EQ(r.tagged[0].name, std::string(""));
    CHECK_EQ(r.triangleCount(), (size_t)0);
}

TEST(obj_at_sign_in_the_middle_is_not_a_tag) {
    auto r = core::parseObj(std::string(kSquare) + "usemtl NOTA@TAG\nf 1 2 3 4\n", mtl(kShipMtl));
    CHECK_EQ(r.tagged.size(), (size_t)0);
    CHECK_EQ(r.triangleCount(), (size_t)2);
    CHECK(!core::isTagMaterial("NOTA@TAG"));
    CHECK(core::isTagMaterial("@X"));
    CHECK(!core::isTagMaterial(""));
}

TEST(obj_tagged_without_mtl_file) {
    TempDir t;
    auto p = t.write("nomtl.obj", std::string("mtllib gone.mtl\n") + kSquare + "usemtl @HUD-INFO\nf 1 2 3 4\nusemtl BODY\nf 1 2 3\n");
    core::ObjParseResult r;
    CHECK(core::parseObjFile(p, r));
    CHECK_EQ(r.tagged.size(), (size_t)1);            // the tag comes from the name, not from the MTL
    CHECK_EQ(r.triangleCount(), (size_t)1);
    CHECK(hasWarning(r, "gone.mtl"));
}

TEST(obj_tagged_material_with_triangle_or_ngon) {
    auto tri = core::parseObj("v 0 0 0\nv 2 0 0\nv 2 1 0\nusemtl @HUD-INFO\nf 1 2 3\n", mtl(kShipMtl));
    CHECK_EQ(tri.tagged.size(), (size_t)1);
    CHECK_EQ(tri.triangleCount(), (size_t)0);
    CHECK(near(tri.tagged[0].width, 2.0f) && near(tri.tagged[0].height, 1.0f));   // completed to the rectangle (0,0)-(2,1)
    CHECK(near(tri.tagged[0].corners[3].y, 1.0f));
    CHECK(hasWarning(tri, "triangle"));

    auto ngon = core::parseObj("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 0 2 0\nusemtl @HUD-INFO\nf 1 2 3 4 5\n", mtl(kShipMtl));
    CHECK_EQ(ngon.tagged.size(), (size_t)1);
    CHECK(hasWarning(ngon, "first four"));

    auto bad = core::parseObj("v 0 0 0\nusemtl @HUD-INFO\nf 1 2 3\nf 1 1\n", mtl(kShipMtl));   // bad tagged faces: skipped
    CHECK_EQ(bad.tagged.size(), (size_t)0);
    CHECK(hasWarning(bad, "tagged"));
}

TEST(obj_tagged_faces_read_from_temp_files_like_shipv2) {
    TempDir t;
    t.write("ShipV2.mtl", kShipMtl);
    auto p = t.write("ShipV2.obj",
        "mtllib ShipV2.mtl\no SHIP\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 5 5 5\nv 6 5 5\nv 6 6 5\nv 5 6 5\n"
        "usemtl BODY\nf 1 2 3 4\nusemtl @HUD-INFO\nf 5/1/1 6/2/1 7/3/1 8/4/1\n");
    core::ObjParseResult r;
    CHECK(core::parseObjFile(p, r));
    CHECK_EQ(r.tagged.size(), (size_t)1);
    CHECK_EQ(r.triangleCount(), (size_t)2);
    CHECK(near(r.tagged[0].centre.x, 5.5f));
}
