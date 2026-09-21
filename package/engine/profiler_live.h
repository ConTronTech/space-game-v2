#pragma once
// Pure logic of the LIVE profiler (no GL, no SDL, no engine types; unit-tested in tests/test_profiler_live.cpp).
// The engine's Profiler feeds a fixed-size ring of the last ~600 frames (frame time + the per-hook / per-pass time of every frame); everything the
// in-game overlay and the F5 dump show is computed from it: rolling windows, 1% low, top consumers, hitch rules, graph bars.
// Nothing here allocates after construction.
#include <array>
#include <string>
#include <vector>

namespace engine::live {

constexpr int kRingFrames = 600;    // ~10 s at 60 fps
constexpr int kMaxSlots = 256;      // distinct module hooks + render passes that can be tracked
constexpr int kMaxHitches = 50;
constexpr double kRefLineMs = 16.7;  // the 60 fps reference line in the graph
constexpr double kAmberMs = 18.0;    // a bar is amber above this (a little over 16.7 so vsync jitter does not colour every bar)
constexpr double kRedMs = 33.3;      // ... and red above this (the 30 fps floor)

// ---- ring buffer ----
class FrameRing {
public:
    FrameRing(int frames = kRingFrames, int slots = kMaxSlots);
    // slotMs: `slots()` floats (0 = that part took no time this frame). May be null (all zero). The only place the ring writes; never allocates.
    void push(float frameMs, const float* slotMs);
    void clear() { count_ = 0; head_ = 0; }
    int size() const { return count_; }
    int capacity() const { return frames_; }
    int slots() const { return slots_; }
    float frameMsAgo(int k) const;                 // k = 0 is the newest frame; 0 when k is out of range
    float slotMsAgo(int k, int slot) const;        // the same for one part

private:
    int index(int k) const { return ((head_ - 1 - k) % frames_ + frames_) % frames_; }
    int frames_, slots_;
    int head_ = 0, count_ = 0;                     // head_ = where the next frame goes
    std::vector<float> frameMs_, slotMs_;          // slotMs_[frame * slots_ + slot]
};

// ---- rolling windows ----
// The newest frames whose summed time fits in windowMs (at least one if any exist; never more than the ring holds).
int framesInWindow(const FrameRing& r, double windowMs);

struct FrameStats {
    int frames = 0;
    double avgMs = 0, worstMs = 0;
    double p99Ms = 0;         // 99th-percentile frame time (nearest rank): 1% of the frames were slower
    double avgFps = 0;        // frames / total seconds
    double lowFps = 0;        // "1% low": 1000 / p99Ms
};
// Statistics over the newest `lastFrames` frames (clamped to what exists). All zeros for an empty ring.
FrameStats statsOver(const FrameRing& r, int lastFrames);

// ---- top consumers ----
struct Consumer { int slot = -1; float avgMs = 0; };
// The `n` parts with the largest average time over the newest `lastFrames` frames, largest first; ties go to the lower slot (so the list is stable).
// skip[slot] != 0 leaves a part out (e.g. a hook whose children are listed separately). Returns how many were written (only parts with avg > 0).
int topConsumers(const FrameRing& r, int lastFrames, int n, Consumer* out, const unsigned char* skip = nullptr);

// ---- hitches ----
struct HitchContext {
    unsigned long frame = 0;    // frame number of the slow frame
    bool paused = false;        // the game was paused during it
    bool prevPaused = false;    // ... or during the frame before (the first frame after unpausing)
    int resizeGrace = 0;        // > 0 during and right after a window resize / fullscreen switch
};
constexpr unsigned long kHitchWarmupFrames = 60;
// A frame counts as a hitch when it is slower than thresholdMs and it is not the start-up (first 60 frames), not paused, not the frame after a pause, not a resize.
bool isHitch(double frameMs, double thresholdMs, const HitchContext& c);

struct Hitch {
    unsigned long frame = 0;
    double timeSec = 0, ms = 0;
    int slot[3] = {-1, -1, -1};  // its three slowest parts (slot ids; -1 = none)
    float partMs[3] = {0, 0, 0};
};
class HitchLog {
public:
    void add(const Hitch& h);                          // keeps the newest kMaxHitches
    int size() const { return count_; }
    const Hitch& at(int i) const;                      // 0 = oldest
    int countSince(double nowSec, double windowSec) const;   // hitches whose time is within the last windowSec seconds
    void clear() { count_ = 0; head_ = 0; }
private:
    std::array<Hitch, kMaxHitches> items_{};
    int head_ = 0, count_ = 0;
};

// ---- graph ----
enum class BarColor { Normal, Amber, Red };
struct Bar { float height = 0; BarColor color = BarColor::Normal; };   // height 0..1 of the graph's full scale
Bar barFor(double frameMs, double fullScaleMs = 50.0);

// ---- text ----
std::string fmt1(double v);                                  // "12.3"
std::string fmtFps(double fps);                              // "59.8"
std::string fmtUptime(double seconds);                       // "1h 02m 03s" / "4m 07s" / "12s"

} // namespace engine::live
