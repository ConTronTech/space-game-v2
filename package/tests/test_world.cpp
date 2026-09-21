#include <cmath>
#include "tests/test.h"
#include "world/skybox/skybox_rules.h"
#include "world/starfield/starfield_rules.h"

namespace {
bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
} // namespace

TEST(world_capped_size_keeps_aspect_and_never_enlarges) {
    auto a = world::cappedSize(8192, 4096, 1024);
    CHECK_EQ(a.w, 1024); CHECK_EQ(a.h, 512);
    auto b = world::cappedSize(4096, 8192, 1024);
    CHECK_EQ(b.w, 512); CHECK_EQ(b.h, 1024);
    auto c = world::cappedSize(512, 256, 1024);            // smaller than the cap: unchanged
    CHECK_EQ(c.w, 512); CHECK_EQ(c.h, 256);
    auto d = world::cappedSize(1024, 1024, 1024);
    CHECK_EQ(d.w, 1024); CHECK_EQ(d.h, 1024);
    auto e = world::cappedSize(8192, 4096, 0);             // <1 counts as 1
    CHECK_EQ(e.w, 1); CHECK_EQ(e.h, 1);
    auto f = world::cappedSize(8192, 4096, -5);
    CHECK_EQ(f.w, 1); CHECK_EQ(f.h, 1);
    auto g = world::cappedSize(10000, 3, 100);             // thin image: short side stays >= 1
    CHECK_EQ(g.w, 100); CHECK_EQ(g.h, 1);
}

TEST(world_face_name_mapping) {
    CHECK_EQ(world::matchFace("front.png"), 0);
    CHECK_EQ(world::matchFace("BACK.PNG"), 1);
    CHECK_EQ(world::matchFace("sky_lf.jpg"), 2);
    CHECK_EQ(world::matchFace("right.png"), 3);
    CHECK_EQ(world::matchFace("top.png"), 4);
    CHECK_EQ(world::matchFace("bot.png"), 5);
    CHECK_EQ(world::matchFace("sky_dn.png"), 5);
    CHECK_EQ(world::matchFace("down.png"), 5);
    CHECK_EQ(world::matchFace("readme.png"), -1);
    CHECK_EQ(std::string(world::faceName(5)), std::string("bot"));
    CHECK_EQ(std::string(world::faceJsonKey(5)), std::string("bottom"));
    for (int i = 0; i < 6; i++) CHECK_EQ(world::matchFace(std::string(world::faceName(i)) + ".png"), i);   // canonical names round-trip
}

TEST(world_face_uv_transform_all_combinations) {
    // corners (0,0) (1,0) (1,1) (0,1), computed by hand from the old skybox.h rules
    struct Case { bool fu, fv; int rot; float expect[4][2]; };
    const Case cases[] = {
        {false, false, 0,   {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
        {true,  false, 0,   {{1, 0}, {0, 0}, {0, 1}, {1, 1}}},
        {false, true,  0,   {{0, 1}, {1, 1}, {1, 0}, {0, 0}}},
        {true,  true,  0,   {{1, 1}, {0, 1}, {0, 0}, {1, 0}}},
        {false, false, 90,  {{0, 1}, {0, 0}, {1, 0}, {1, 1}}},
        {false, false, 180, {{1, 1}, {0, 1}, {0, 0}, {1, 0}}},
        {false, false, 270, {{1, 0}, {1, 1}, {0, 1}, {0, 0}}},
        {true,  true,  90,  {{1, 0}, {1, 1}, {0, 1}, {0, 0}}},
    };
    const float in[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (auto& c : cases) {
        world::FaceUV uv; uv.flipU = c.fu; uv.flipV = c.fv; uv.rotate = c.rot;
        for (int k = 0; k < 4; k++) {
            float u, v;
            uv.transform(in[k][0], in[k][1], u, v);
            CHECK(near(u, c.expect[k][0])); CHECK(near(v, c.expect[k][1]));
        }
    }
    CHECK_EQ(world::normalizeRotate(450), 90);
    CHECK_EQ(world::normalizeRotate(-90), 270);
    CHECK_EQ(world::normalizeRotate(45), 0);
}

TEST(world_choose_set) {
    std::vector<std::string> names = {"blue/set1", "dark/set1", "dark/set2"};
    CHECK_EQ(world::chooseSet(names, "dark/set2"), 2);
    CHECK_EQ(world::chooseSet(names, "dark/set1"), 1);
    CHECK_EQ(world::chooseSet(names, "nope/set9"), 0);      // fallback: first
    CHECK_EQ(world::chooseSet(names, ""), 0);
    CHECK_EQ(world::chooseSet({}, "dark/set1"), -1);        // nothing found
}

TEST(world_warp_streak_length) {
    CHECK(near(world::warpStreakLength(false, 2000, 2000, 300), 0));     // not warping
    CHECK(near(world::warpStreakLength(true, 0, 2000, 300), 0));
    CHECK(near(world::warpStreakLength(true, 1000, 2000, 300), 150));    // proportional
    CHECK(near(world::warpStreakLength(true, 2000, 2000, 300), 300));
    CHECK(near(world::warpStreakLength(true, 9000, 2000, 300), 300));    // capped
    CHECK(near(world::warpStreakLength(true, 2000, 2000, 0), 0));        // disabled by tunable
    CHECK(near(world::warpStreakLength(true, 2000, 0, 300), 0));         // bad ref speed
}

TEST(world_cache_path_and_freshness) {
    CHECK_EQ(world::cachePath("cache", "dark", "set1", 1024, "front"), std::string("cache/skybox/dark_set1_1024/front.png"));
    CHECK_EQ(world::cachePath("c", "red", "set3", 0, "bot"), std::string("c/skybox/red_set3_1/bot.png"));
    CHECK(world::cacheFresh(true, 200, 100));
    CHECK(world::cacheFresh(true, 100, 100));
    CHECK(!world::cacheFresh(true, 99, 100));                // source is newer
    CHECK(!world::cacheFresh(false, 500, 100));              // missing
}

TEST(world_box_downscale_averages_and_handles_pitch) {
    // 4x2 RGBA (bpp 4, padded pitch 20) -> 2x1: each output pixel averages a 2x2 block
    std::vector<uint8_t> src(20 * 2, 0);
    auto put = [&](int x, int y, int r, int g, int b) { uint8_t* p = &src[y * 20 + x * 4]; p[0] = r; p[1] = g; p[2] = b; p[3] = 255; };
    put(0, 0, 0, 0, 0);     put(1, 0, 100, 0, 0);   put(2, 0, 200, 200, 200); put(3, 0, 200, 200, 200);
    put(0, 1, 100, 0, 0);   put(1, 1, 0, 0, 0);     put(2, 1, 200, 200, 200); put(3, 1, 200, 200, 200);
    std::vector<uint8_t> out;
    world::boxDownscale(src.data(), 4, 2, 20, 4, 2, 1, out);
    CHECK_EQ(out.size(), (size_t)6);
    CHECK_EQ((int)out[0], 50); CHECK_EQ((int)out[1], 0); CHECK_EQ((int)out[2], 0);
    CHECK_EQ((int)out[3], 200); CHECK_EQ((int)out[4], 200); CHECK_EQ((int)out[5], 200);
    // no scaling: pixels copied, alpha dropped
    world::boxDownscale(src.data(), 4, 2, 20, 4, 4, 2, out);
    CHECK_EQ(out.size(), (size_t)24);
    CHECK_EQ((int)out[3], 100);
    // non-integer ratio never reads out of bounds (ASan checks) and stays in range
    std::vector<uint8_t> big(7 * 5 * 3, 128);
    world::boxDownscale(big.data(), 7, 5, 21, 3, 3, 2, out);
    for (auto v : out) CHECK_EQ((int)v, 128);
}

TEST(world_effective_face_uv_implicit_flip_on_top_and_bottom) {
    for (int face = 0; face < 6; face++) {
        bool vertical = face == 4 || face == 5;
        for (int fu = 0; fu < 2; fu++) for (int fv = 0; fv < 2; fv++) for (int rot : {0, 90, 180, 270}) {
            world::FaceUV j; j.flipU = fu; j.flipV = fv; j.rotate = rot;
            auto e = world::effectiveFaceUV(face, j);
            CHECK_EQ(e.flipV, vertical ? !(bool)fv : (bool)fv);   // top/bottom toggled, sides unchanged; json flip_v cancels it
            CHECK_EQ(e.flipU, (bool)fu);
            CHECK_EQ(e.rotate, rot);
        }
    }
    world::FaceUV none;
    CHECK(world::effectiveFaceUV(4, none).flipV);                 // no json: implicit flip alone
    world::FaceUV j; j.flipV = true;
    CHECK(!world::effectiveFaceUV(5, j).flipV);                   // json flip_v cancels it
}
