#include <cmath>
#include "core/audio/audio_synth.h"
#include "tests/test.h"

using namespace core::synth;

TEST(audio_mix_gain_multiplies_and_clamps) {
    CHECK_EQ(mixGain(100, 100, 1.0f), 1.0f);
    CHECK_EQ(mixGain(50, 50, 1.0f), 0.25f);
    CHECK_EQ(mixGain(80, 50, 0.5f), 0.2f);
    CHECK_EQ(mixGain(0, 100, 1.0f), 0.0f);          // master off = silence
    CHECK_EQ(mixGain(200, 200, 5.0f), 1.0f);        // out-of-range inputs never exceed full scale
    CHECK_EQ(mixGain(-10, 100, 1.0f), 0.0f);
    CHECK_EQ(mixGain(100, 100, -1.0f), 0.0f);
}

TEST(audio_blip_shape) {
    auto b = blip(880.0f, 0.06f);
    CHECK_EQ(b.size(), (size_t)(0.06f * kRate));
    CHECK_EQ((int)b[0], 0);                          // starts silent (attack), no click
    int peak = 0;
    for (auto s : b) peak = std::max(peak, std::abs((int)s));
    CHECK(peak > 3000 && peak < 20000);              // audible, not clipping
    CHECK(std::abs((int)b.back()) < peak / 4);       // decayed by the end
}

TEST(audio_hum_loops_seamlessly_and_never_clips) {
    auto h = hum(2.0f);
    CHECK_EQ(h.size(), (size_t)(2.0f * kRate));
    // the sample after the last one is sample 0 again: the step across the seam must be no bigger than a normal step
    int seam = std::abs((int)h[0] - (int)h.back());
    int maxStep = 0;
    for (size_t i = 1; i < h.size(); i++) maxStep = std::max(maxStep, std::abs((int)h[i] - (int)h[i - 1]));
    CHECK(seam <= maxStep);
    int peak = 0;
    for (auto s : h) peak = std::max(peak, std::abs((int)s));
    CHECK(peak > 5000);
    CHECK(peak < 32000);
}
