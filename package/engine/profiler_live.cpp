#include "engine/profiler_live.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace engine::live {

FrameRing::FrameRing(int frames, int slots) : frames_(std::max(1, frames)), slots_(std::max(1, slots)) {
    frameMs_.assign((size_t)frames_, 0.0f);
    slotMs_.assign((size_t)frames_ * (size_t)slots_, 0.0f);
}

void FrameRing::push(float frameMs, const float* slotMs) {
    frameMs_[(size_t)head_] = frameMs;
    float* row = &slotMs_[(size_t)head_ * (size_t)slots_];
    if (slotMs) std::memcpy(row, slotMs, (size_t)slots_ * sizeof(float));
    else std::fill(row, row + slots_, 0.0f);
    head_ = (head_ + 1) % frames_;
    if (count_ < frames_) count_++;
}

float FrameRing::frameMsAgo(int k) const { return k >= 0 && k < count_ ? frameMs_[(size_t)index(k)] : 0.0f; }
float FrameRing::slotMsAgo(int k, int slot) const {
    if (k < 0 || k >= count_ || slot < 0 || slot >= slots_) return 0.0f;
    return slotMs_[(size_t)index(k) * (size_t)slots_ + (size_t)slot];
}

int framesInWindow(const FrameRing& r, double windowMs) {
    int n = 0;
    double sum = 0;
    while (n < r.size()) {
        double ms = r.frameMsAgo(n);
        if (n > 0 && sum + ms > windowMs) break;      // the newest frame is always included, even if it alone is longer than the window
        sum += ms;
        n++;
    }
    return n;
}

FrameStats statsOver(const FrameRing& r, int lastFrames) {
    FrameStats s;
    int n = std::clamp(lastFrames, 0, r.size());
    if (n == 0) return s;
    std::array<float, kRingFrames> buf;               // on the stack: no allocation. (If a bigger ring is ever used, only the newest kRingFrames count.)
    n = std::min(n, (int)buf.size());
    double sum = 0;
    for (int k = 0; k < n; k++) {
        float ms = r.frameMsAgo(k);
        buf[(size_t)k] = ms;
        sum += ms;
        s.worstMs = std::max(s.worstMs, (double)ms);
    }
    s.frames = n;
    s.avgMs = sum / n;
    s.avgFps = sum > 0 ? n * 1000.0 / sum : 0;
    size_t idx = (size_t)std::ceil(0.99 * n);                   // nearest-rank percentile, like bench::compute
    idx = std::min((size_t)n, std::max<size_t>(1, idx)) - 1;
    std::nth_element(buf.begin(), buf.begin() + (long)idx, buf.begin() + n);
    s.p99Ms = buf[idx];
    s.lowFps = s.p99Ms > 0 ? 1000.0 / s.p99Ms : 0;
    return s;
}

int topConsumers(const FrameRing& r, int lastFrames, int n, Consumer* out, const unsigned char* skip) {
    int frames = std::clamp(lastFrames, 0, r.size());
    if (frames == 0 || n <= 0) return 0;
    std::array<float, kMaxSlots> sums{};
    const int slots = std::min(r.slots(), (int)sums.size());
    for (int k = 0; k < frames; k++)
        for (int s = 0; s < slots; s++) sums[(size_t)s] += r.slotMsAgo(k, s);
    int written = 0;
    for (int rank = 0; rank < n; rank++) {                      // n is small (6): repeated selection beats a sort
        int best = -1;
        for (int s = 0; s < slots; s++) {
            if (skip && skip[s]) continue;
            bool taken = false;
            for (int j = 0; j < written; j++) if (out[j].slot == s) { taken = true; break; }
            if (taken || sums[(size_t)s] <= 0) continue;
            if (best < 0 || sums[(size_t)s] > sums[(size_t)best]) best = s;   // strict >: on a tie the lower slot (found first) wins
        }
        if (best < 0) break;
        out[written++] = {best, sums[(size_t)best] / (float)frames};
    }
    return written;
}

bool isHitch(double frameMs, double thresholdMs, const HitchContext& c) {
    if (!(frameMs > thresholdMs)) return false;
    if (c.frame < kHitchWarmupFrames) return false;
    if (c.paused || c.prevPaused) return false;
    if (c.resizeGrace > 0) return false;
    return true;
}

void HitchLog::add(const Hitch& h) {
    items_[(size_t)head_] = h;
    head_ = (head_ + 1) % kMaxHitches;
    if (count_ < kMaxHitches) count_++;
}
const Hitch& HitchLog::at(int i) const {
    static const Hitch none;
    if (i < 0 || i >= count_) return none;
    int oldest = ((head_ - count_) % kMaxHitches + kMaxHitches) % kMaxHitches;
    return items_[(size_t)((oldest + i) % kMaxHitches)];
}
int HitchLog::countSince(double nowSec, double windowSec) const {
    int n = 0;
    for (int i = 0; i < count_; i++) if (nowSec - at(i).timeSec <= windowSec) n++;
    return n;
}

Bar barFor(double frameMs, double fullScaleMs) {
    Bar b;
    double ms = std::isfinite(frameMs) ? std::max(0.0, frameMs) : 0.0;
    b.height = fullScaleMs > 0 ? (float)std::min(1.0, ms / fullScaleMs) : 0.0f;
    b.color = ms > kRedMs ? BarColor::Red : ms > kAmberMs ? BarColor::Amber : BarColor::Normal;
    return b;
}

std::string fmt1(double v) { char b[32]; std::snprintf(b, sizeof b, "%.1f", v); return b; }
std::string fmtFps(double fps) { return fmt1(fps); }
std::string fmtUptime(double s) {
    long t = (long)std::max(0.0, s);
    char b[48];
    if (t >= 3600) std::snprintf(b, sizeof b, "%ldh %02ldm %02lds", t / 3600, (t / 60) % 60, t % 60);
    else if (t >= 60) std::snprintf(b, sizeof b, "%ldm %02lds", t / 60, t % 60);
    else std::snprintf(b, sizeof b, "%lds", t);
    return b;
}

} // namespace engine::live
