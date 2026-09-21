#include "engine/profiler.h"
#include <algorithm>
#include <cstdio>

namespace engine {

Profiler::Profiler() { touched_.reserve(live::kMaxSlots * 2); }   // add() never allocates in the frame loop

void Profiler::enableLive(bool on) {
    if (!on) { ring_.reset(); return; }
    if (!ring_) ring_ = std::make_unique<live::FrameRing>();
}

int Profiler::intern(const std::string& name) {
    auto it = index_.find(name);
    if (it != index_.end()) return it->second;
    int id = (int)stats_.size();
    Stat s;
    s.name = name;
    stats_.push_back(std::move(s));
    index_.emplace(name, id);
    if (name == "core/render_engine:render") containerId_ = id;
    return id;
}

void Profiler::beginFrame(unsigned long frameNumber, bool paused, double timeSec) {
    frameNo_ = frameNumber;
    curPaused_ = paused;
    timeSec_ = timeSec;
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

void Profiler::endFrame(double frameMs, bool pausedNow) {
    curPaused_ = curPaused_ || pausedNow;
    if (ring_) {
        row_.fill(0.0f);
        for (int id : touched_) if (id < live::kMaxSlots) row_[(size_t)id] = (float)stats_[(size_t)id].cur;
        ring_->push((float)frameMs, row_.data());
        live::HitchContext hc{frameNo_, curPaused_, prevPaused_, resizeGrace_};
        if (live::isHitch(frameMs, hitchMs_, hc)) {                 // remember it with its three slowest parts (the "render" container is left out: its passes are listed)
            live::Hitch h;
            h.frame = frameNo_; h.timeSec = timeSec_; h.ms = frameMs;
            for (int id : touched_) {
                if (id == containerId_) continue;
                float ms = (float)stats_[(size_t)id].cur;
                for (int k = 0; k < 3; k++) {
                    if (h.slot[k] < 0 || ms > h.partMs[k]) {
                        for (int j = 2; j > k; j--) { h.slot[j] = h.slot[j - 1]; h.partMs[j] = h.partMs[j - 1]; }
                        h.slot[k] = id; h.partMs[k] = ms;
                        break;
                    }
                }
            }
            hitches_.add(h);
        }
    }
    prevPaused_ = curPaused_;
    if (resizeGrace_ > 0) resizeGrace_--;
    if (!detailed_) return;
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
    if (!note_.empty()) o += "settings: " + note_ + "\n";
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

std::string Profiler::liveDump(const std::vector<std::string>& header) const {
    std::string o;
    char b[320];
    o += "SPACE GAME V2 - LIVE PROFILE SNAPSHOT (F5)\n";
    o += "==========================================\n";
    for (const std::string& h : header) o += h + "\n";
    if (!ring_ || ring_->size() == 0) { o += "\n(no frames recorded: profiler.lite is off or the game just started)\n"; return o; }
    const live::FrameRing& r = *ring_;
    live::FrameStats all = live::statsOver(r, r.size());
    std::snprintf(b, sizeof b, "\nFRAMES: the last %d frames (%.1f s): average %.2f ms (%.1f fps), worst %.2f ms, 1%% low %.1f fps (99th percentile %.2f ms)\n",
                  all.frames, all.frames * all.avgMs / 1000.0, all.avgMs, all.avgFps, all.worstMs, all.lowFps, all.p99Ms);
    o += b;
    o += "HOW TO READ THIS: each row is CPU wall-clock time per frame. 'module:hook' = time inside that module's hook (update, render, ui, present...). 'pass:name' = one render pass; passes run\n"
         "inside 'core/render_engine:render', so do not add the two. 'core/window:present' is the buffer swap: when the GPU is the bottleneck the time shows up THERE (or in the first pass\n"
         "of the next frame), not in the pass that is heavy. If the header says GPU MODE the passes were measured with glFinish (serialised: fps is lower, but the ranking of passes is right).\n"
         "'engine:unaccounted' = frame time outside every hook. 'cockpit:solid/screens/glass' are parts of 'pass:ship/cockpit'. With vsync ON, 'core/window:present' includes waiting for the\n"
         "screen refresh, so a healthy 60 fps game shows ~16 ms there: look at the OTHER rows and at the worst-ms column. '% frame' is of the average frame.\n\n";
    std::snprintf(b, sizeof b, "%-46s %9s %9s %8s\n", "AGGREGATE (name)", "avg ms", "worst ms", "% frame");
    o += b;
    struct Row2 { int slot; double avg, worst; };
    std::vector<Row2> rows;
    for (int s = 0; s < std::min(r.slots(), (int)stats_.size()); s++) {
        double sum = 0, worst = 0;
        for (int k = 0; k < r.size(); k++) { double v = r.slotMsAgo(k, s); sum += v; worst = std::max(worst, v); }
        if (sum > 0) rows.push_back({s, sum / r.size(), worst});
    }
    std::sort(rows.begin(), rows.end(), [](const Row2& a, const Row2& c) { return a.avg > c.avg; });
    for (const Row2& w : rows) {
        if (w.avg < 0.002 && w.worst < 2.0) continue;          // hide rows that never matter
        std::snprintf(b, sizeof b, "%-46s %9.3f %9.3f %7.1f%%\n", slotName(w.slot).c_str(), w.avg, w.worst, all.avgMs > 0 ? 100.0 * w.avg / all.avgMs : 0.0);
        o += b;
    }
    std::snprintf(b, sizeof b, "\nHITCHES (frames slower than %.0f ms, not counting start-up, pauses and window resizes): %d recorded (newest 50 kept)\n", hitchMs_, hitches_.size());
    o += b;
    for (int i = 0; i < hitches_.size(); i++) {
        const live::Hitch& h = hitches_.at(i);
        std::snprintf(b, sizeof b, "  frame %7lu  at %8.2f s  %7.2f ms   slowest parts: ", h.frame, h.timeSec, h.ms);
        o += b;
        bool first = true;
        for (int k = 0; k < 3; k++) {
            if (h.slot[k] < 0) continue;
            std::snprintf(b, sizeof b, "%s%s %.2f", first ? "" : ", ", slotName(h.slot[k]).c_str(), h.partMs[k]);
            o += b;
            first = false;
        }
        o += "\n";
    }
    if (hitches_.size() == 0) o += "  none\n";
    o += "\nLAST 120 FRAME TIMES (ms, oldest first, 10 per line):\n";
    int n = std::min(120, r.size());
    for (int i = 0; i < n; i++) {
        std::snprintf(b, sizeof b, "%6.1f", r.frameMsAgo(n - 1 - i));
        o += b;
        if (i % 10 == 9 || i == n - 1) o += "\n";
    }
    return o;
}

} // namespace engine
