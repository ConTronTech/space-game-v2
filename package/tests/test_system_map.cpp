#include <cmath>
#include <limits>
#include "ui/system_map/system_map_rules.h"
#include "tests/test.h"

namespace {
bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
} // namespace

// Worked example, seed-1234 orbit radii from docs/GRAVITY.md, default zoom 400,000 (knee 8,000): f = log1p(d / 8000) / log1p(50)
TEST(system_map_log_projection_worked_example) {
    CHECK(near(sysmap::radialFraction(46493, 400000), 0.4878, 1e-3));    // Planet 1
    CHECK(near(sysmap::radialFraction(140369, 400000), 0.7429, 1e-3));   // Planet 3
    CHECK(near(sysmap::radialFraction(352200, 400000), 0.9683, 1e-3));   // Planet 6: inside the rim
    CHECK(near(sysmap::radialFraction(400000, 400000), 1.0, 1e-9));      // the rim
    CHECK(near(sysmap::radialFraction(1e7, 400000), 1.0, 1e-9));         // clamped
    CHECK(near(sysmap::radialFraction(0, 400000), 0.0, 1e-12));
    CHECK(near(sysmap::radialFraction(100, 0), 0.0, 1e-12));             // a degenerate range never divides by zero
    // monotonic: the planets keep their order
    double prev = 0;
    for (double r : {46493.0, 90501.0, 140369.0, 218380.0, 280726.0, 352200.0}) { double f = sysmap::radialFraction(r, 400000); CHECK(f > prev); prev = f; }
}

TEST(system_map_project_directions) {
    auto p = sysmap::project(46493, 0, 0, 0, 400000, 500, 300, 200);    // +x = right
    CHECK(near(p.x, 500 + 0.4878 * 200, 0.3) && near(p.y, 300, 1e-3));
    auto q = sysmap::project(0, 46493, 0, 0, 400000, 500, 300, 200);    // +z = down
    CHECK(near(q.x, 500, 1e-3) && q.y > 300);
    auto c = sysmap::project(5, 5, 5, 5, 400000, 500, 300, 200);        // at the focus: the centre, no NaN
    CHECK(c.x == 500 && c.y == 300);
    // a moon 1,319 from Planet 3, focused on the planet at zoom 2,000: clearly apart
    auto m = sysmap::project(140369 + 1319, 0, 140369, 0, 2000, 0, 0, 200);
    CHECK(m.x > 150);
}

TEST(system_map_view_clamping) {
    sysmap::Limits L; L.zoomMin = 500; L.zoomMax = 1e6; L.panMax = 1e6;
    sysmap::View v; v.range = 10;
    CHECK(near(sysmap::clampView(v, L).range, 500, 1e-9));
    v.range = 1e9; CHECK(near(sysmap::clampView(v, L).range, 1e6, 1e-9));
    v.range = std::numeric_limits<double>::quiet_NaN(); CHECK(std::isfinite(sysmap::clampView(v, L).range));
    v = {}; v.fx = 3e6; v.fz = 0; auto c = sysmap::clampView(v, L); CHECK(near(c.fx, 1e6, 1e-6));
    v.fx = std::numeric_limits<double>::infinity(); CHECK(sysmap::clampView(v, L).fx == 0);
    sysmap::Limits bad; bad.zoomMin = -5; bad.zoomMax = -10; bad.panMax = -1;   // bad limits are sanitised
    auto b = sysmap::clampView({400000, 10, 10}, bad);
    CHECK(b.range >= 1.0 && b.fx == 0 && b.fz == 0);
    // zoom steps: in and out return to the start, and stop at the limits
    sysmap::View z; z.range = 400000;
    CHECK(near(sysmap::zoomed(sysmap::zoomed(z, 1, L), -1, L).range, 400000, 1e-6));
    for (int i = 0; i < 100; i++) z = sysmap::zoomed(z, 1, L);
    CHECK(near(z.range, 500, 1e-9));
    // pan moves by a quarter of the range
    sysmap::View p; p.range = 1000;
    CHECK(near(sysmap::panned(p, 1, -1, L).fx, 250, 1e-9) && near(sysmap::panned(p, 1, -1, L).fz, -250, 1e-9));
}

TEST(system_map_markers) {
    CHECK(near(sysmap::markerSize(sysmap::Mark::Planet, 198), 3.66, 0.01));
    CHECK(near(sysmap::markerSize(sysmap::Mark::Planet, 5000), 7.0, 1e-6));   // capped
    CHECK(sysmap::markerSize(sysmap::Mark::Moon, 252) < sysmap::markerSize(sysmap::Mark::Planet, 198));
    CHECK(sysmap::markerSize(sysmap::Mark::Sun, 1781) > sysmap::markerSize(sysmap::Mark::Planet, 1134));
    float dark[3] = {0.1f, 0.05f, 0.0f}, out[3];
    sysmap::markerColor(dark, 0.55f, out);
    CHECK(near(out[0], 0.55, 1e-5) && near(out[1], 0.275, 1e-5) && out[2] == 0.0f);   // hue kept, lifted
    float bright[3] = {0.9f, 0.8f, 0.7f};
    sysmap::markerColor(bright, 0.55f, out);
    CHECK(near(out[0], 0.9, 1e-6));
    CHECK(sysmap::moonMinOffset(4, 1) > sysmap::moonMinOffset(4, 0));
}

TEST(system_map_belt_band) {
    std::vector<double> r;
    for (int i = 0; i < 1500; i++) r.push_back(170000 + (i % 300) * 10.0);   // a belt 170,000..172,990
    for (int i = 0; i < 240; i++) r.push_back(i % 2 ? 46493 : 280726);     // clusters around planets
    auto b = sysmap::beltBand(r);
    CHECK(b.valid && near(b.inner, 170000, 1e-6) && near(b.outer, 172990, 1e-6));
    std::vector<double> few{1, 2, 3};
    CHECK(!sysmap::beltBand(few).valid);
}
