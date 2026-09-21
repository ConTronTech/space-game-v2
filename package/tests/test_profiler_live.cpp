// Tests for the LIVE profiler: ring buffer, rolling windows, 1% low, top consumers, hitch rules, graph bars, text, and the Profiler's live recorder.
#include <cmath>
#include "engine/profiler.h"
#include "engine/profiler_live.h"
#include "tests/test.h"

namespace {
using namespace engine::live;
bool nearl(double a, double b, double eps = 1e-4) { return std::fabs(a - b) < eps; }
void pushFrames(FrameRing& r, int n, float ms, int startValue = 0) { for (int i = 0; i < n; i++) r.push(ms + (float)(startValue + i) * 0.0f, nullptr); }
}

TEST(live_ring_wraps_and_keeps_the_newest_frames) {
    FrameRing r(10, 4);
    for (int i = 1; i <= 25; i++) { float row[4] = {(float)i, 0, (float)i * 2, 0}; r.push((float)i, row); }
    CHECK_EQ(r.size(), 10);
    CHECK(nearl(r.frameMsAgo(0), 25) && nearl(r.frameMsAgo(9), 16));               // newest 25 ... oldest kept 16
    CHECK(nearl(r.frameMsAgo(10), 0) && nearl(r.frameMsAgo(-1), 0));               // out of range: 0, no crash
    CHECK(nearl(r.slotMsAgo(0, 2), 50) && nearl(r.slotMsAgo(9, 2), 32));           // the per-part rows wrap with the frames
    CHECK(nearl(r.slotMsAgo(0, 9), 0) && nearl(r.slotMsAgo(99, 0), 0));            // bad slot / frame: 0
    r.clear();
    CHECK_EQ(r.size(), 0);
    CHECK(nearl(r.frameMsAgo(0), 0));
}
TEST(live_ring_default_capacity_and_null_rows) {
    FrameRing r;
    CHECK_EQ(r.capacity(), kRingFrames);
    CHECK_EQ(r.slots(), kMaxSlots);
    for (int i = 0; i < kRingFrames + 50; i++) r.push(16.0f, nullptr);             // a null row means "all parts zero"
    CHECK_EQ(r.size(), kRingFrames);
    CHECK(nearl(r.slotMsAgo(3, 7), 0));
}
TEST(live_window_selection_by_time) {
    FrameRing r(600, 4);
    pushFrames(r, 100, 10.0f);                                                     // 10 ms frames
    CHECK_EQ(framesInWindow(r, 1000), 100);                                        // exactly 1000 ms fits
    CHECK_EQ(framesInWindow(r, 250), 25);
    CHECK_EQ(framesInWindow(r, 5), 1);                                             // at least the newest frame, even if it is longer than the window
    CHECK_EQ(framesInWindow(r, 1e9), 100);                                         // never more than exist
    FrameRing e(10, 2);
    CHECK_EQ(framesInWindow(e, 1000), 0);
    // across the wrap-around: 30 frames into a ring of 10, the window sees only what is still stored
    FrameRing w(10, 2);
    for (int i = 0; i < 30; i++) w.push(20.0f, nullptr);
    CHECK_EQ(framesInWindow(w, 1000), 10);
    CHECK_EQ(framesInWindow(w, 100), 5);
}
TEST(live_stats_avg_worst_and_one_percent_low) {
    FrameRing r(600, 2);
    for (int i = 0; i < 99; i++) r.push(10.0f, nullptr);
    r.push(50.0f, nullptr);                                                        // one slow frame in 100
    FrameStats s = statsOver(r, 100);
    CHECK_EQ(s.frames, 100);
    CHECK(nearl(s.avgMs, 10.4) && nearl(s.worstMs, 50.0));
    CHECK(nearl(s.avgFps, 1000.0 / 10.4, 1e-3));
    CHECK(nearl(s.p99Ms, 10.0));                                                   // nearest rank: the 99th of 100 is still a normal frame
    CHECK(nearl(s.lowFps, 100.0));
    r.push(50.0f, nullptr);                                                        // 2 slow frames in the newest 100
    s = statsOver(r, 100);
    CHECK(nearl(s.p99Ms, 50.0) && nearl(s.lowFps, 20.0));                          // now 1% of the frames were that slow
    // a window that wraps around the ring end gives the same answer as a straight one
    FrameRing w(20, 2);
    for (int i = 0; i < 47; i++) w.push(i % 5 == 0 ? 40.0f : 16.0f, nullptr);
    FrameStats sw = statsOver(w, 20);
    CHECK_EQ(sw.frames, 20);
    CHECK(sw.worstMs == 40.0);
}
TEST(live_stats_with_very_few_frames_and_empty_ring) {
    FrameRing r(600, 2);
    FrameStats z = statsOver(r, 100);
    CHECK_EQ(z.frames, 0);
    CHECK(z.avgFps == 0 && z.lowFps == 0);
    r.push(20.0f, nullptr);
    FrameStats one = statsOver(r, 100);
    CHECK_EQ(one.frames, 1);
    CHECK(nearl(one.avgMs, 20) && nearl(one.lowFps, 50) && nearl(one.p99Ms, 20));  // a single frame: its own 1% low
    r.push(40.0f, nullptr);
    FrameStats two = statsOver(r, 2);
    CHECK(nearl(two.p99Ms, 40) && nearl(two.lowFps, 25));                          // few frames: the slowest is the 99th percentile
    CHECK_EQ(statsOver(r, -5).frames, 0);
    CHECK_EQ(statsOver(r, 1).frames, 1);
}
TEST(live_top_consumers_ranking_ties_and_skip) {
    FrameRing r(100, 8);
    for (int i = 0; i < 10; i++) {
        float row[8] = {1.0f, 5.0f, 3.0f, 3.0f, 0, 9.0f, 0.5f, 0};                // slot 5 is biggest, 2 and 3 tie, slot 4 and 7 are zero
        r.push(20.0f, row);
    }
    Consumer out[6];
    int n = topConsumers(r, 10, 6, out);
    CHECK_EQ(n, 6);                                                                // zero rows are not listed (slots 4 and 7)
    CHECK(out[0].slot == 5 && nearl(out[0].avgMs, 9.0));
    CHECK(out[1].slot == 1 && nearl(out[1].avgMs, 5.0));
    CHECK(out[2].slot == 2 && out[3].slot == 3);                                   // the tie goes to the lower slot, every time
    CHECK(out[4].slot == 0 && nearl(out[4].avgMs, 1.0));
    CHECK(out[5].slot == 6 && nearl(out[5].avgMs, 0.5));
    n = topConsumers(r, 10, 3, out);
    CHECK_EQ(n, 3);
    CHECK(out[2].slot == 2);
    unsigned char skip[8] = {0, 0, 0, 0, 0, 1, 0, 0};                              // leave the container out
    n = topConsumers(r, 10, 2, out, skip);
    CHECK(n == 2 && out[0].slot == 1 && out[1].slot == 2);
    // averaging over a window: only the newest frames count
    FrameRing w(100, 2);
    for (int i = 0; i < 10; i++) { float row[2] = {100.0f, 0}; w.push(16, row); }
    for (int i = 0; i < 10; i++) { float row[2] = {0, 4.0f}; w.push(16, row); }
    Consumer c[2];
    n = topConsumers(w, 10, 2, c);
    CHECK(n == 1 && c[0].slot == 1 && nearl(c[0].avgMs, 4.0));                     // the newest 10 frames only
    n = topConsumers(w, 20, 2, c);
    CHECK(n == 2 && c[0].slot == 0 && nearl(c[0].avgMs, 50.0) && nearl(c[1].avgMs, 2.0));
    CHECK_EQ(topConsumers(w, 0, 2, c), 0);
    CHECK_EQ(topConsumers(w, 10, 0, c), 0);
}
TEST(live_hitch_rules_warmup_pause_resize) {
    HitchContext c;
    c.frame = 500;
    CHECK(isHitch(41.0, 40.0, c));
    CHECK(!isHitch(40.0, 40.0, c));                                                // strictly slower than the threshold
    CHECK(!isHitch(12.0, 40.0, c));
    c.frame = 59;
    CHECK(!isHitch(200.0, 40.0, c));                                               // start-up: asset loading is not a stutter
    c.frame = 60;
    CHECK(isHitch(200.0, 40.0, c));
    c.frame = 500; c.paused = true;
    CHECK(!isHitch(200.0, 40.0, c));                                               // paused
    c.paused = false; c.prevPaused = true;
    CHECK(!isHitch(200.0, 40.0, c));                                               // the frame right after unpausing
    c.prevPaused = false; c.resizeGrace = 1;
    CHECK(!isHitch(200.0, 40.0, c));                                               // a window resize / fullscreen switch
    c.resizeGrace = 0;
    CHECK(isHitch(200.0, 40.0, c));
    CHECK(!isHitch(std::nan(""), 40.0, c));                                        // garbage is never a hitch
}
TEST(live_hitch_log_is_bounded_ordered_and_counts_recent) {
    HitchLog log;
    for (int i = 0; i < 120; i++) { Hitch h; h.frame = (unsigned long)i; h.timeSec = (double)i; h.ms = 50; log.add(h); }
    CHECK_EQ(log.size(), kMaxHitches);
    CHECK_EQ(log.at(0).frame, 70ul);                                               // oldest kept
    CHECK_EQ(log.at(kMaxHitches - 1).frame, 119ul);                                // newest last
    CHECK_EQ(log.at(-1).frame, 0ul);
    CHECK_EQ(log.at(999).frame, 0ul);
    CHECK_EQ(log.countSince(119.0, 30.0), 31);                                     // times 89..119
    CHECK_EQ(log.countSince(119.0, 0.5), 1);
    CHECK_EQ(log.countSince(1000.0, 30.0), 0);
    HitchLog empty;
    CHECK_EQ(empty.countSince(10, 30), 0);
}
TEST(live_graph_bars_heights_and_colours) {
    Bar a = barFor(8.0), b = barFor(20.0), c = barFor(34.0), d = barFor(200.0);
    CHECK(a.color == BarColor::Normal && b.color == BarColor::Amber && c.color == BarColor::Red && d.color == BarColor::Red);
    CHECK(nearl(a.height, 8.0 / 50.0) && nearl(d.height, 1.0));                    // clamped to the full scale
    CHECK(barFor(16.7).color == BarColor::Normal && barFor(17.9).color == BarColor::Normal);   // vsync jitter around 16.7 stays plain
    CHECK(barFor(18.1).color == BarColor::Amber && barFor(33.3).color == BarColor::Amber && barFor(33.4).color == BarColor::Red);
    CHECK(nearl(barFor(-4).height, 0) && nearl(barFor(std::nan("")).height, 0));   // bad input: an empty bar
    CHECK(nearl(barFor(10, 0).height, 0));                                         // zero scale does not divide by zero
    CHECK(nearl(barFor(kRefLineMs).height, 16.7 / 50.0));                          // where the 60 fps line is drawn
}
TEST(live_text_formatting) {
    CHECK_EQ(fmt1(12.34), std::string("12.3"));
    CHECK_EQ(fmtFps(59.96), std::string("60.0"));
    CHECK_EQ(fmtUptime(0), std::string("0s"));
    CHECK_EQ(fmtUptime(59.9), std::string("59s"));
    CHECK_EQ(fmtUptime(247), std::string("4m 07s"));
    CHECK_EQ(fmtUptime(3723), std::string("1h 02m 03s"));
    CHECK_EQ(fmtUptime(-5), std::string("0s"));
}

// ---- the Profiler's live recorder ----
TEST(profiler_live_recorder_fills_the_ring_and_captures_hitches) {
    engine::Profiler p;
    p.enableLive(true);
    p.setDetailed(false);
    CHECK(p.liveEnabled());
    int a = p.intern("core/render_engine:render"), b = p.intern("pass:skybox"), c = p.intern("mod:update"), d = p.intern("pass:stars"), e = p.intern("mod:ui");
    for (unsigned long f = 0; f < 200; f++) {
        p.beginFrame(f, false, (double)f / 60.0);
        p.add(a, 5.0); p.add(b, 2.0); p.add(c, 1.0);
        double ms = 16.0;
        if (f == 150) { p.add(d, 60.0); p.add(e, 3.0); p.add(a, 61.0); ms = 90.0; }   // a 90 ms hitch: pass:stars 60, render container 66, ui 3, skybox 2
        p.endFrame(ms);
    }
    CHECK_EQ(p.ring()->size(), 200);
    CHECK(std::fabs(p.ring()->frameMsAgo(49) - 90.0) < 1e-4);                     // frame 150 is 49 frames ago
    CHECK(std::fabs(p.ring()->slotMsAgo(0, b) - 2.0) < 1e-4);
    CHECK_EQ(p.hitches().size(), 1);
    const engine::live::Hitch& h = p.hitches().at(0);
    CHECK_EQ(h.frame, 150ul);
    CHECK(std::fabs(h.ms - 90.0) < 1e-9 && std::fabs(h.timeSec - 2.5) < 1e-9);
    CHECK_EQ(p.slotName(h.slot[0]), std::string("pass:stars"));                   // the slowest part; the render container is left out
    CHECK_EQ(p.slotName(h.slot[1]), std::string("mod:ui"));
    CHECK_EQ(p.slotName(h.slot[2]), std::string("pass:skybox"));
    CHECK(h.slot[0] != a && h.slot[1] != a && h.slot[2] != a);
    CHECK(p.rows().empty());                                                       // detailed off: no cumulative table was kept
}
TEST(profiler_live_hitches_ignore_startup_pauses_and_resizes) {
    engine::Profiler p;
    p.enableLive(true);
    int a = p.intern("x");
    auto frame = [&](unsigned long f, bool paused, double ms) { p.beginFrame(f, paused, (double)f); p.add(a, 1.0); p.endFrame(ms, paused); };
    for (unsigned long f = 0; f < 10; f++) frame(f, false, 500.0);                  // start-up loading
    CHECK_EQ(p.hitches().size(), 0);
    for (unsigned long f = 10; f < 100; f++) frame(f, false, 16.0);
    frame(100, true, 300.0);                                                        // paused (menu open)
    frame(101, false, 300.0);                                                       // the first frame after unpausing
    CHECK_EQ(p.hitches().size(), 0);
    frame(102, false, 300.0);                                                       // a real one
    CHECK_EQ(p.hitches().size(), 1);
    frame(103, false, 16.0);
    p.noteResize();                                                                 // the window was resized during frame 104
    frame(104, false, 300.0);
    frame(105, false, 300.0);                                                       // and the next one too
    CHECK_EQ(p.hitches().size(), 1);
    frame(106, false, 300.0);
    CHECK_EQ(p.hitches().size(), 2);
    p.setHitchMs(400.0);
    frame(107, false, 300.0);
    CHECK_EQ(p.hitches().size(), 2);                                                // the threshold is tunable
}
TEST(profiler_live_disabled_records_nothing_and_detailed_mode_still_works) {
    engine::Profiler p;                                                             // defaults: detailed (old behaviour), live off
    CHECK(!p.liveEnabled() && p.ring() == nullptr);
    int a = p.intern("x");
    for (unsigned long f = 30; f < 40; f++) { p.beginFrame(f); p.add(a, 2.0); p.endFrame(10.0); }
    CHECK_EQ(p.frames(), 10ul);
    CHECK(std::fabs(p.rows()[0].avgMs - 2.0) < 1e-9);
    CHECK(p.liveDump({"h"}).find("no frames recorded") != std::string::npos);      // the dump says so instead of crashing
    p.enableLive(true);
    p.enableLive(false);
    CHECK(!p.liveEnabled());
    p.setGpuMode(true);
    CHECK(p.gpuMode());
}
TEST(profiler_live_dump_is_self_explanatory) {
    engine::Profiler p;
    p.enableLive(true);
    int a = p.intern("core/window:present"), b = p.intern("pass:skybox");
    for (unsigned long f = 0; f < 150; f++) {
        p.beginFrame(f, false, (double)f / 60.0);
        p.add(a, 10.0); p.add(b, 3.0);
        p.endFrame(f == 120 ? 80.0 : 14.0);
    }
    std::string d = p.liveDump({"quality:        low", "GL renderer:    Test GPU"});
    CHECK(d.find("LIVE PROFILE SNAPSHOT") != std::string::npos);
    CHECK(d.find("quality:        low") != std::string::npos && d.find("Test GPU") != std::string::npos);   // the caller's header comes first
    CHECK(d.find("HOW TO READ THIS") != std::string::npos);
    CHECK(d.find("AGGREGATE") != std::string::npos && d.find("core/window:present") != std::string::npos && d.find("pass:skybox") != std::string::npos);
    CHECK(d.find("HITCHES") != std::string::npos && d.find("frame     120") != std::string::npos && d.find("80.00 ms") != std::string::npos);
    CHECK(d.find("LAST 120 FRAME TIMES") != std::string::npos);
    CHECK(d.find("80.0") != std::string::npos);                                     // the hitch shows in the frame-time list too
}
