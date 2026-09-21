#pragma once
// Frame profiler (see docs/PERFORMANCE.md). Pure (no GL, no SDL): the engine times every module phase, RenderEngine times every pass, and this
// class folds the samples into reports.
//   LIVE (always on, `profiler.lite`, default true): a ring of the last 600 frames + hitch capture, read by the in-game overlay (F3) and the F5 dump.
//   --profile        DETAILED: cumulative table + every slow frame, printed at exit (logs/profile.txt)
//   --profile=gpu    the same, with glFinish() around every render pass so GPU work is charged to the pass that caused it
//                    (this serialises CPU and GPU and slows the frame: use it to find WHICH pass is heavy, not for absolute fps). F4 toggles it live.
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "engine/profiler_live.h"

namespace engine {

class Profiler {
public:
    static constexpr double kSlowFrameMs = 33.0;      // default: frames slower than this are listed individually (--profile-slow=MS changes it)
    void setSlowMs(double ms) { slowMs_ = ms > 0 ? ms : kSlowFrameMs; }
    static constexpr unsigned long kWarmupFrames = 30; // start-up frames (asset loads) are left out of the table averages
    static constexpr size_t kMaxSlowFrames = 200;

    Profiler();
    int intern(const std::string& name);              // stable id for a name (creates it)
    void beginFrame(unsigned long frameNumber, bool paused = false, double timeSec = 0);
    void add(int id, double ms);                      // ms spent in 'id' during the current frame (adds up if called twice)
    void endFrame(double frameMs, bool pausedNow = false);   // frameMs: wall time of the whole frame including the buffer swap

    // ---- live recorder (ring buffer + hitch capture); allocates once in enableLive, never afterwards ----
    void enableLive(bool on);
    bool liveEnabled() const { return ring_ != nullptr; }
    const live::FrameRing* ring() const { return ring_.get(); }
    const live::HitchLog& hitches() const { return hitches_; }
    void setHitchMs(double ms) { hitchMs_ = ms > 0 ? ms : 40.0; }
    double hitchMs() const { return hitchMs_; }
    void noteResize() { resizeGrace_ = 2; }           // a window resize / fullscreen switch: this frame and the next are not hitches
    double timeSec() const { return timeSec_; }       // engine time at the start of the current frame
    int slotCount() const { return (int)stats_.size(); }
    const std::string& slotName(int id) const { static const std::string none; return id >= 0 && id < (int)stats_.size() ? stats_[(size_t)id].name : none; }
    bool isContainer(int id) const { return id == containerId_; }   // the "render" hook contains the pass rows: leave it out of top lists
    void setGpuMode(bool on) { gpuMode_ = on; }       // glFinish around every render pass (RenderEngine reads this every frame)
    bool gpuMode() const { return gpuMode_; }
    void setDetailed(bool on) { detailed_ = on; }     // the cumulative table + slow-frame list (--profile). Default true (tests, old behaviour); the engine sets it from the flag.
    bool detailed() const { return detailed_; }
    // The F5 snapshot: 'header' lines (build, quality, GL renderer...), the aggregate table over the ring, the hitch list, the last 120 frame times.
    std::string liveDump(const std::vector<std::string>& header) const;

    struct Row { std::string name; double avgMs = 0, worstMs = 0, percent = 0; };
    struct SlowFrame { unsigned long frame = 0; double ms = 0; std::vector<std::pair<std::string, double>> top; };
    std::vector<Row> rows() const;                    // sorted by average, largest first
    const std::vector<SlowFrame>& slowFrames() const { return slow_; }
    unsigned long frames() const { return counted_; } // frames counted in the averages (after the warm-up)
    double avgFrameMs() const { return counted_ ? totalFrameMs_ / (double)counted_ : 0; }
    void setNote(std::string note) { note_ = std::move(note); }   // one line printed in the report header (e.g. the render scale in use)
    std::string report() const;                       // the table + slow-frame list as text

private:
    struct Stat { std::string name; double sum = 0, worst = 0; double cur = 0; };
    std::unique_ptr<live::FrameRing> ring_;
    live::HitchLog hitches_;
    std::array<float, live::kMaxSlots> row_{};
    double hitchMs_ = 40.0, timeSec_ = 0;
    bool curPaused_ = false, prevPaused_ = false, gpuMode_ = false, detailed_ = true;
    int resizeGrace_ = 0, containerId_ = -1;
    std::vector<Stat> stats_;
    std::unordered_map<std::string, int> index_;
    std::vector<int> touched_;                        // ids that got samples this frame
    unsigned long frameNo_ = 0, counted_ = 0;
    double totalFrameMs_ = 0, worstFrameMs_ = 0;
    std::vector<SlowFrame> slow_;
    size_t slowDropped_ = 0;
    double slowMs_ = kSlowFrameMs;
    std::string note_;
};

// RAII sample: times its own lifetime into the profiler; does nothing when p is null.
class ProfScope {
public:
    ProfScope(Profiler* p, int id) : p_(p), id_(id) { if (p_) t0_ = std::chrono::steady_clock::now(); }
    ~ProfScope() { if (p_) p_->add(id_, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0_).count()); }
    ProfScope(const ProfScope&) = delete;
    ProfScope& operator=(const ProfScope&) = delete;
private:
    Profiler* p_;
    int id_;
    std::chrono::steady_clock::time_point t0_;
};

} // namespace engine
