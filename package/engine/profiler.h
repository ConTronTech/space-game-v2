#pragma once
// Frame profiler, INERT unless the game is started with --profile (see docs/PERFORMANCE.md). Pure (no GL, no SDL): the engine
// times every module phase, RenderEngine times every pass, and this class folds the samples into a report.
//   --profile        CPU wall-clock time of each module phase and render pass
//   --profile=gpu    the same, with glFinish() around every render pass so GPU work is charged to the pass that caused it
//                    (this serialises CPU and GPU and slows the frame: use it to find WHICH pass is heavy, not for absolute fps)
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

class Profiler {
public:
    static constexpr double kSlowFrameMs = 33.0;      // default: frames slower than this are listed individually (--profile-slow=MS changes it)
    void setSlowMs(double ms) { slowMs_ = ms > 0 ? ms : kSlowFrameMs; }
    static constexpr unsigned long kWarmupFrames = 30; // start-up frames (asset loads) are left out of the table averages
    static constexpr size_t kMaxSlowFrames = 200;

    int intern(const std::string& name);              // stable id for a name (creates it)
    void beginFrame(unsigned long frameNumber);
    void add(int id, double ms);                      // ms spent in 'id' during the current frame (adds up if called twice)
    void endFrame(double frameMs);                    // frameMs: wall time of the whole frame including the buffer swap

    struct Row { std::string name; double avgMs = 0, worstMs = 0, percent = 0; };
    struct SlowFrame { unsigned long frame = 0; double ms = 0; std::vector<std::pair<std::string, double>> top; };
    std::vector<Row> rows() const;                    // sorted by average, largest first
    const std::vector<SlowFrame>& slowFrames() const { return slow_; }
    unsigned long frames() const { return counted_; } // frames counted in the averages (after the warm-up)
    double avgFrameMs() const { return counted_ ? totalFrameMs_ / (double)counted_ : 0; }
    std::string report() const;                       // the table + slow-frame list as text

private:
    struct Stat { std::string name; double sum = 0, worst = 0; double cur = 0; };
    std::vector<Stat> stats_;
    std::unordered_map<std::string, int> index_;
    std::vector<int> touched_;                        // ids that got samples this frame
    unsigned long frameNo_ = 0, counted_ = 0;
    double totalFrameMs_ = 0, worstFrameMs_ = 0;
    std::vector<SlowFrame> slow_;
    size_t slowDropped_ = 0;
    double slowMs_ = kSlowFrameMs;
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
