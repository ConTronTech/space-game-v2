#pragma once
// Pure functions (no SDL): procedural sounds and volume math, so they can be unit-tested.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace core::synth {

constexpr int kRate = 44100;

// gain = master% * bus% * volume, each clamped; inputs are 0..100 for master/bus and 0..1 for volume
inline float mixGain(float masterPct, float busPct, float volume) {
    auto c01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
    return c01(masterPct / 100.0f) * c01(busPct / 100.0f) * c01(volume);
}

// Short UI blip: sine with a 2 ms attack and exponential decay.
inline std::vector<int16_t> blip(float freqHz, float seconds) {
    size_t n = (size_t)(seconds * kRate);
    std::vector<int16_t> out(n);
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / kRate;
        float env = std::min(1.0f, t / 0.002f) * std::exp(-t * 6.0f / seconds);
        out[i] = (int16_t)(std::sin(2.0f * (float)M_PI * freqHz * t) * env * 0.5f * 32767.0f);
    }
    return out;
}

// Low engine rumble that loops seamlessly: every partial has a whole number of cycles in 'seconds'
// (use a multiple of 2 s), and the slow wobble is a whole number of cycles too.
inline std::vector<int16_t> hum(float seconds = 2.0f) {
    size_t n = (size_t)(seconds * kRate);
    std::vector<int16_t> out(n);
    const float partials[4][2] = {{55.0f, 1.0f}, {82.5f, 0.6f}, {110.0f, 0.4f}, {165.0f, 0.2f}};   // Hz, amplitude
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / kRate, s = 0;
        for (auto& p : partials) s += p[1] * std::sin(2.0f * (float)M_PI * p[0] * t);
        float wobble = 0.85f + 0.15f * std::sin(2.0f * (float)M_PI * 1.0f * t);
        out[i] = (int16_t)(s / 2.2f * wobble * 0.6f * 32767.0f);
    }
    return out;
}

} // namespace core::synth
