#include <cmath>
#include "world/atmosphere/atmosphere_rules.h"
#include "tests/test.h"

namespace {
bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
}

TEST(atmosphere_rim_alpha_bright_at_the_limb_clear_head_on) {
    CHECK(near(world::rimAlpha(0.0f, 3.0f, 0.85f), 0.85, 1e-5));          // edge-on: full glow
    CHECK(near(world::rimAlpha(1.0f, 3.0f, 0.85f), 0.0, 1e-5));           // straight down: clear
    CHECK(world::rimAlpha(0.95f, 3.0f, 0.85f) < 0.01f);                     // near head-on: nearly clear
    CHECK(near(world::rimAlpha(0.5f, 3.0f, 1.0f), 0.125, 1e-5));          // (1 - 0.5)^3
    float prev = 2.0f;
    for (int i = 0; i <= 20; i++) {                                         // monotonic: falls as the view turns head-on
        float a = world::rimAlpha(i / 20.0f, 3.0f, 0.85f);
        CHECK(a <= prev + 1e-6f); CHECK(a >= 0.0f && a <= 0.85f);
        prev = a;
    }
    CHECK(near(world::rimAlpha(-0.3f, 3.0f, 0.85f), world::rimAlpha(0.3f, 3.0f, 0.85f), 1e-6));   // inside (c < 0) = same curve
    CHECK(near(world::rimAlpha(-1.0f, 3.0f, 0.85f), 0.0, 1e-5));          // straight up from inside: clear
    CHECK(near(world::rimAlpha(5.0f, 3.0f, 0.85f), 0.0, 1e-5));           // out of range is clamped
    CHECK(world::rimAlpha(0.3f, 6.0f, 1.0f) < world::rimAlpha(0.3f, 2.0f, 1.0f));   // higher power = thinner limb
}

TEST(atmosphere_sun_factor_day_bright_night_floor) {
    CHECK(near(world::sunFactor(1.0f, 0.15f), 1.0, 1e-5));
    CHECK(near(world::sunFactor(-1.0f, 0.15f), 0.15, 1e-5));
    CHECK(world::sunFactor(0.0f, 0.15f) > 0.5f);                           // the glow wraps a little past the terminator
    CHECK(world::sunFactor(-0.5f, 0.15f) < world::sunFactor(0.0f, 0.15f));
}

TEST(atmosphere_shell_range_and_distance_fade) {
    CHECK(near(world::shellRadius(400, 1.06f), 424, 1e-3));
    CHECK(near(world::shellRadius(400, 0.5f), 400, 1e-3));                 // never inside the planet
    CHECK(near(world::visibleRange(400, 12), 4800, 1e-6));
    CHECK(near(world::distanceFade(1000, 4800, 0.7f), 1.0, 1e-6));
    CHECK(near(world::distanceFade(4800, 4800, 0.7f), 0.0, 1e-6));
    CHECK(near(world::distanceFade(9000, 4800, 0.7f), 0.0, 1e-6));
    float mid = world::distanceFade(4080, 4800, 0.7f);                     // halfway through the fade band
    CHECK(near(mid, 0.5, 1e-4));
    CHECK(world::distanceFade(3500, 4800, 0.7f) > world::distanceFade(4500, 4800, 0.7f));
}

TEST(atmosphere_inside_tint_zero_at_boundary_max_at_surface) {
    const float r = 400, R = 424, m = 0.12f;
    CHECK(near(world::insideTint(500, r, R, m), 0.0, 1e-6));              // outside
    CHECK(near(world::insideTint(424, r, R, m), 0.0, 1e-6));              // at the boundary: no pop
    CHECK(near(world::insideTint(400, r, R, m), 0.12, 1e-5));             // at the surface: the max
    CHECK(near(world::insideTint(300, r, R, m), 0.12, 1e-5));             // below the surface: clamped
    float prev = -1;
    for (int i = 0; i <= 24; i++) { float t = world::insideTint(R - i, r, R, m); CHECK(t >= prev - 1e-7f); CHECK(t <= m + 1e-6f); prev = t; }
    CHECK(near(world::insideTint(412, r, R, m), 0.12 * 0.75, 1e-5));      // half depth: ease-out 0.75 of the max
    CHECK(near(world::insideTint(410, r, r, m), 0.0, 1e-6));              // degenerate shell: nothing
}

TEST(atmosphere_colour_is_blue_shifted_and_clamped) {
    const float grey[3] = {0.5f, 0.5f, 0.5f}, white[3] = {5, 5, 5};
    float c[3];
    world::atmosphereColor(grey, c);
    CHECK(near(c[0], 0.4, 1e-5)); CHECK(near(c[1], 0.5, 1e-5)); CHECK(near(c[2], 0.65, 1e-5));
    CHECK(c[2] > c[1] && c[1] > c[0]);
    world::atmosphereColor(white, c);
    CHECK(c[0] <= 1.0f && c[1] <= 1.0f && c[2] <= 1.0f);
    CHECK_EQ(world::shellVertexCount(24, 16), 425);
}
