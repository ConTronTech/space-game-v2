#include "engine/profiler.h"
#include <algorithm>
#include <cstdio>

namespace engine {

int Profiler::intern(const std::string& name) {
    auto it = index_.find(name);
    if (it != index_.end()) return it->second;
    int id = (int)stats_.size();
    Stat s;
    s.name = name;
    stats_.push_back(std::move(s));
    index_.emplace(name, id);
    return id;
}

void Profiler::beginFrame(unsigned long frameNumber) {
    frameNo_ = frameNumber;
    for (int id : touched_) stats_[(size_t)id].cur = 0;
    touched_.clear();
}

void Profiler::add(int id, double ms) {
    if (id < 0 || (size_t)id >= stats_.size()) return;
    Stat& s = stats_[(size_t)id];
    if (s.cur == 0) touched_.push_back(id);
    s.cur += ms;
    if (s.cur == 0) s.cur = 1e-9;                      // keep "touched" true for a zero-length sample
}

void Profiler::endFrame(double frameMs) {
    const bool counted = frameNo_ >= kWarmupFrames;
    if (counted) {
        counted_++;
        totalFrameMs_ += frameMs;
        worstFrameMs_ = std::max(worstFrameMs_, frameMs);
        for (int id : touched_) {
            Stat& s = stats_[(size_t)id];
            s.sum += s.cur;
            s.worst = std::max(s.worst, s.cur);
        }
    }
    if (frameMs > slowMs_) {
        if (slow_.size() >= kMaxSlowFrames) { slowDropped_++; return; }
        SlowFrame f;
        f.frame = frameNo_;
        f.ms = frameMs;
        for (int id : touched_) f.top.emplace_back(stats_[(size_t)id].name, stats_[(size_t)id].cur);
        std::sort(f.top.begin(), f.top.end(), [](auto& a, auto& b) { return a.second > b.second; });
        if (f.top.size() > 3) f.top.resize(3);
        slow_.push_back(std::move(f));
    }
}

std::vector<Profiler::Row> Profiler::rows() const {
    std::vector<Row> out;
    for (const Stat& s : stats_) {
        if (s.sum <= 0) continue;
        Row r;
        r.name = s.name;
        r.avgMs = counted_ ? s.sum / (double)counted_ : 0;
        r.worstMs = s.worst;
        r.percent = totalFrameMs_ > 0 ? 100.0 * s.sum / totalFrameMs_ : 0;
        out.push_back(std::move(r));
    }
    std::sort(out.begin(), out.end(), [](const Row& a, const Row& b) { return a.avgMs > b.avgMs; });
    return out;
}

std::string Profiler::report() const {
    std::string o;
    char b[256];
    std::snprintf(b, sizeof b, "PROFILE: %lu frames after a %lu-frame warm-up, average frame %.2f ms (%.1f fps), worst %.2f ms\n",
                  counted_, kWarmupFrames, avgFrameMs(), avgFrameMs() > 0 ? 1000.0 / avgFrameMs() : 0.0, worstFrameMs_);
    o += b;
    o += "(phases are 'module:hook', passes are 'pass:name'; a pass runs inside render:core/render_engine, so do not add the two)\n";
    std::snprintf(b, sizeof b, "%-46s %9s %9s %7s\n", "name", "avg ms", "worst ms", "% frame");
    o += b;
    for (const Row& r : rows()) {
        if (r.avgMs < 0.002 && r.worstMs < 2.0) continue;   // hide rows that never matter
        std::snprintf(b, sizeof b, "%-46s %9.3f %9.3f %6.1f%%\n", r.name.c_str(), r.avgMs, r.worstMs, r.percent);
        o += b;
    }
    std::snprintf(b, sizeof b, "\nSLOW FRAMES (> %.0f ms): %zu%s\n", slowMs_, slow_.size(), slowDropped_ ? " (list truncated)" : "");
    o += b;
    for (const SlowFrame& f : slow_) {
        std::snprintf(b, sizeof b, "  frame %6lu  %7.2f ms  ", f.frame, f.ms);
        o += b;
        for (size_t i = 0; i < f.top.size(); i++) {
            std::snprintf(b, sizeof b, "%s%s %.2f", i ? ", " : "", f.top[i].first.c_str(), f.top[i].second);
            o += b;
        }
        o += "\n";
    }
    return o;
}

} // namespace engine
