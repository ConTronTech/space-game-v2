#include "engine/config.h"
#include "tests/test.h"
#include "core/quality/quality_rules.h"

using namespace quality;

namespace {
Detection det(const char* renderer, const char* vendor = "", int tex = 16384, int threads = 8, int ram = 16000, int w = 1920, int h = 1080) {
    Hardware hw; hw.renderer = renderer; hw.vendor = vendor; hw.maxTexture = tex; hw.cpuThreads = threads; hw.ramMb = ram; hw.displayW = w; hw.displayH = h;
    return detect(hw);
}
}

TEST(quality_detects_software_renderers_as_low) {
    CHECK(det("llvmpipe (LLVM 15.0.7, 256 bits)", "Mesa").preset == Preset::Low);
    CHECK(det("softpipe").preset == Preset::Low);
    CHECK(det("Software Rasterizer").preset == Preset::Low);
}

TEST(quality_detects_intel_ironlake_as_low_and_modern_intel_as_medium) {
    CHECK(det("Mesa DRI Intel(R) Ironlake Mobile", "Intel Open Source Technology Center", 8192, 4, 4096, 1366, 768).preset == Preset::Low);
    CHECK(det("Mesa DRI Intel(R) HD Graphics 4000", "Intel").preset == Preset::Low);
    CHECK(det("Mesa Intel(R) UHD Graphics 620 (KBL GT2)", "Intel").preset == Preset::Medium);
    CHECK(det("Intel(R) Iris(R) Xe Graphics", "Intel").preset == Preset::Medium);
    CHECK(det("Intel(R) Arc(tm) A770 Graphics", "Intel").preset == Preset::High);
}

TEST(quality_detects_nvidia_and_amd) {
    Detection d = det("NVIDIA GeForce RTX 2060 SUPER/PCIe/SSE2", "NVIDIA Corporation");
    CHECK(d.preset == Preset::Ultra);
    CHECK(!d.reason.empty());
    CHECK(det("NVIDIA GeForce GTX 1060/PCIe/SSE2", "NVIDIA Corporation").preset == Preset::High);
    CHECK(det("NVIDIA GeForce GT 710/PCIe/SSE2", "NVIDIA Corporation").preset == Preset::Medium);
    CHECK(det("AMD Radeon RX 6700 XT (navi22, LLVM 15.0.7, DRM 3.49)", "AMD").preset == Preset::Ultra);
    CHECK(det("AMD Radeon RX 580 Series (polaris10, LLVM 15)", "AMD").preset == Preset::High);
    CHECK(det("AMD Radeon", "AMD").preset == Preset::Medium);
    CHECK(det("AMD Radeon Graphics (renoir, LLVM 15.0.7)", "AMD").preset == Preset::Medium);
}

TEST(quality_unknown_and_empty_hardware_is_medium) {
    CHECK(det("").preset == Preset::Medium);
    CHECK(det("Something Weird 9000", "Acme").preset == Preset::Medium);
}

TEST(quality_weak_cpu_ram_texture_or_4k_only_lower_the_result) {
    CHECK(det("NVIDIA GeForce RTX 2060 SUPER/PCIe/SSE2", "NVIDIA", 16384, 2, 16000).preset == Preset::Medium);
    CHECK(det("NVIDIA GeForce RTX 2060 SUPER/PCIe/SSE2", "NVIDIA", 16384, 8, 2048).preset == Preset::Medium);
    CHECK(det("NVIDIA GeForce RTX 2060 SUPER/PCIe/SSE2", "NVIDIA", 2048).preset == Preset::Low);
    CHECK(det("NVIDIA GeForce RTX 2060 SUPER/PCIe/SSE2", "NVIDIA", 16384, 8, 16000, 3840, 2160).preset == Preset::High);
    CHECK(det("llvmpipe", "Mesa", 16384, 2, 1024).preset == Preset::Low);      // never raised
    CHECK(det("Mesa DRI Intel(R) Ironlake Mobile").reason.find("Ironlake") != std::string::npos);
}

TEST(quality_preset_parsing_and_cycling) {
    CHECK(parsePreset("high").preset == Preset::High && !parsePreset("high").unknown);
    CHECK(parsePreset("ULTRA").preset == Preset::Ultra);
    CHECK(parsePreset("").preset == Preset::Auto && !parsePreset("").unknown);
    CHECK(parsePreset("potato").preset == Preset::Auto && parsePreset("potato").unknown);
    CHECK(cyclePreset(Preset::Auto, 1) == Preset::Low);
    CHECK(cyclePreset(Preset::Ultra, 1) == Preset::Auto);
    CHECK(cyclePreset(Preset::Auto, -1) == Preset::Ultra);
    CHECK(cyclePreset(Preset::Medium, -1) == Preset::Low);
}

TEST(quality_table_grows_with_the_preset) {
    for (auto& e : presetTable()) {
        CHECK(e.low > 0 || std::string(e.key) == "world.sphere_detail");
        if (std::string(e.key).find("edge_px") != std::string::npos) CHECK(e.low >= e.medium && e.medium >= e.high && e.high >= e.ultra);   // smaller = more detail
        else CHECK(e.low <= e.medium && e.medium <= e.high && e.high <= e.ultra);
    }
    CHECK_EQ(valueFor(presetTable()[0], Preset::Low), 512.0);
    CHECK_EQ(valueFor(presetTable()[0], Preset::Ultra), 2048.0);
}

TEST(config_preset_layer_replaces_the_default_but_never_the_user_value) {
    std::string path = "/tmp/sgv2_test_quality_game.json";
    { std::ofstream f(path); f << "{\"a\": {\"b\": 7, \"c\": \"DEFAULT\"}}"; }
    engine::Config c(path);
    c.load();
    c.setPresetNumber("a.b", 99);
    c.setPresetNumber("a.c", 55);
    c.setPresetNumber("a.d", 12);
    c.setPresetBool("a.flag", true);
    c.setPresetString("a.name", "hello");
    CHECK_EQ(c.get("a.b", 1, ""), 7);              // game.json wins over the preset
    CHECK_EQ(c.get("a.c", 1, ""), 55);             // "DEFAULT" means: not overridden -> the preset applies
    CHECK_EQ(c.get("a.d", 1, ""), 12);             // no game.json entry: the preset replaces the code default
    CHECK_EQ(c.get("a.d", 1.5f, ""), 12.0f);
    CHECK_EQ(c.get("a.e", 3, ""), 3);              // no preset: the code default
    CHECK(c.get("a.flag", false, ""));
    CHECK_EQ(c.get<std::string>("a.name", "x", ""), std::string("hello"));
    c.clearPreset();
    CHECK_EQ(c.get("a.d", 1, ""), 1);
    CHECK_EQ(c.presetSize(), 0u);
    std::remove(path.c_str());
}
