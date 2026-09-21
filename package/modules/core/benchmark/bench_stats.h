#pragma once
// Pure frame-time statistics for the --benchmark mode (no SDL/GL/engine types, unit-tested).
#include <algorithm>
#include <cmath>
#include <vector>

namespace bench {

struct Stats {
    int frames = 0;
    double seconds = 0;         // total time covered by the samples
    double avgMs = 0, worstMs = 0;
    double p99Ms = 0;           // 99th-percentile frame time: 1% of frames were slower than this
    double avgFps = 0;          // frames / seconds
    double lowFps = 0;          // "1% low": 1000 / p99Ms
};

// frameMs: duration of each frame in milliseconds. Empty input gives all zeros.
inline Stats compute(const std::vector<double>& frameMs) {
    Stats s;
    if (frameMs.empty()) return s;
    s.frames = (int)frameMs.size();
    double sum = 0;
    for (double v : frameMs) { sum += v; s.worstMs = std::max(s.worstMs, v); }
    s.seconds = sum / 1000.0;
    s.avgMs = sum / s.frames;
    std::vector<double> sorted = frameMs;
    std::sort(sorted.begin(), sorted.end());
    size_t idx = (size_t)std::ceil(0.99 * sorted.size());       // nearest-rank percentile
    idx = std::min(sorted.size(), std::max<size_t>(1, idx)) - 1;
    s.p99Ms = sorted[idx];
    s.avgFps = s.seconds > 0 ? s.frames / s.seconds : 0;
    s.lowFps = s.p99Ms > 0 ? 1000.0 / s.p99Ms : 0;
    return s;
}

} // namespace bench
