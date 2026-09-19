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

// Impact thud: a short low sine that sweeps down, with a burst of noise at the start (deterministic, so it is testable).
inline std::vector<int16_t> thud(float seconds = 0.35f) {
    size_t n = (size_t)(seconds * kRate);
    std::vector<int16_t> out(n);
    uint32_t seed = 12345;
    float phase = 0;
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / kRate;
        float freq = 40.0f + 55.0f * std::exp(-t * 18.0f);              // ~95 Hz falling to ~40 Hz
        phase += 2.0f * (float)M_PI * freq / kRate;
        seed = seed * 1664525u + 1013904223u;
        float noise = ((seed >> 9) / (float)(1u << 23)) * 2.0f - 1.0f;   // -1..1
        float env = std::min(1.0f, t / 0.002f) * std::exp(-t * 9.0f);
        float s = std::sin(phase) * 0.8f + noise * 0.5f * std::exp(-t * 60.0f);
        out[i] = (int16_t)(s * env * 0.9f * 32767.0f);
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
