// Drives the real Engine::run with test modules to check ordering, skipping and failure handling.
#include <stdexcept>
#include "engine/engine.h"
#include "tests/test.h"

static std::vector<std::string> g_order;

template <int Tag> struct Named { static constexpr int tag = Tag; };

#define TEST_MODULE(Cls, ModName, DEPS, PRIO, BODY)                              \
    class Cls : public engine::Module {                                          \
    public:                                                                      \
        const char* name() const override { return ModName; }                    \
        std::vector<std::string> dependencies() const override { return DEPS; } \
        int priority() const override { return PRIO; }                           \
        bool init(engine::Engine& eng) override { (void)eng; BODY }             \
    };                                                                           \
    REGISTER_MODULE(Cls);

#define OK  g_order.push_back(name()); return true;

TEST_MODULE(TBase,    "t/base",       {},                  0,  OK)
TEST_MODULE(TChild,   "t/child",      {"t/base"},          0,  OK)
TEST_MODULE(TGrand,   "t/grandchild", {"t/child"},         0,  OK)
TEST_MODULE(TEarly,   "t/early",      {},                  -5, OK)
TEST_MODULE(TLate,    "t/late",       {},                  5,  OK)
TEST_MODULE(TMissing, "t/missing_dep",{"t/does_not_exist"},0,  OK)
TEST_MODULE(TFalse,   "t/init_false", {},                  0,  g_order.push_back("t/init_false_ATTEMPT"); return false;)
TEST_MODULE(TAfterF,  "t/after_false",{"t/init_false"},    0,  OK)
TEST_MODULE(TThrow,   "t/init_throws",{},                  0,  g_order.push_back("t/init_throws_ATTEMPT"); throw std::runtime_error("boom");)

class TRequired : public engine::Module {
public:
    const char* name() const override { return "t/required"; }
    bool required() const override { return true; }
    bool init(engine::Engine& eng) override { return !eng.hasFlag("fail-required"); }
};
REGISTER_MODULE(TRequired);

static int runEngine(std::vector<std::string> flags) {
    std::vector<std::string> args = {"test", "--frames=1", "--disable=core/input_handler"};
    for (auto& f : flags) args.push_back(f);
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    engine::Engine e;
    return e.run((int)argv.size(), argv.data());
}

static int indexOf(const std::string& n) {
    for (size_t i = 0; i < g_order.size(); i++) if (g_order[i] == n) return (int)i;
    return -1;
}

TEST(modules_load_in_dependency_then_priority_order) {
    g_order.clear();
    CHECK_EQ(runEngine({}), 0);
    CHECK(indexOf("t/base") >= 0);
    CHECK(indexOf("t/base") < indexOf("t/child"));
    CHECK(indexOf("t/child") < indexOf("t/grandchild"));
    CHECK(indexOf("t/early") < indexOf("t/late"));   // lower priority number first
    CHECK(indexOf("t/early") < indexOf("t/base"));
}

TEST(modules_with_missing_or_failed_deps_are_skipped) {
    g_order.clear();
    CHECK_EQ(runEngine({}), 0);                       // non-required failures don't stop the game
    CHECK_EQ(indexOf("t/missing_dep"), -1);           // never initialised
    CHECK(indexOf("t/init_false_ATTEMPT") >= 0);      // tried...
    CHECK_EQ(indexOf("t/after_false"), -1);           // ...its dependent was skipped
    CHECK(indexOf("t/init_throws_ATTEMPT") >= 0);     // throwing init is a failed init, not a crash
    CHECK(indexOf("t/base") >= 0);                    // and everything else still loaded
}

TEST(disable_flag_removes_a_module) {
    g_order.clear();
    CHECK_EQ(runEngine({"--disable=t/base"}), 0);
    CHECK_EQ(indexOf("t/base"), -1);
    CHECK_EQ(indexOf("t/child"), -1);                 // its dependents can't load either
}

TEST(disable_flag_accepts_repeats_and_lists) {
    g_order.clear();
    CHECK_EQ(runEngine({"--disable=t/early,t/late", "--disable=t/base"}), 0);
    CHECK_EQ(indexOf("t/early"), -1);
    CHECK_EQ(indexOf("t/late"), -1);
    CHECK_EQ(indexOf("t/base"), -1);
}

TEST(required_module_failing_aborts_with_error_code) {
    g_order.clear();
    CHECK_EQ(runEngine({"--fail-required"}), 1);
}

// ---- alpha (fixed-step interpolation factor) ----
static std::vector<float> g_alphas;
static std::vector<bool> g_pausedFlags;
class TAlpha : public engine::Module {
public:
    const char* name() const override { return "t/alpha"; }
    void onUpdate(engine::Engine& eng, float) override {
        if (!eng.hasFlag("test-alpha")) return;
        if (eng.frame() == 3) eng.setPaused(true);
        g_alphas.push_back(eng.alpha());
        g_pausedFlags.push_back(eng.paused());
    }
};
REGISTER_MODULE(TAlpha);

TEST(alpha_is_a_fraction_and_exactly_one_when_paused) {
    g_alphas.clear(); g_pausedFlags.clear();
    std::vector<std::string> args = {"test", "--frames=6", "--test-alpha", "--disable=core/input_handler"};
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    engine::Engine e;
    CHECK_EQ(e.run((int)argv.size(), argv.data()), 0);
    CHECK_EQ(g_alphas.size(), (size_t)6);
    for (size_t i = 0; i < g_alphas.size(); i++) {
        CHECK(g_alphas[i] >= 0.0f && g_alphas[i] <= 1.0f);
        // the frame that triggers the pause was computed just before it; every frame after must be exactly 1
        if (i > 0 && g_pausedFlags[i - 1]) CHECK_EQ(g_alphas[i], 1.0f);
    }
    CHECK(g_pausedFlags.back());     // the pause actually happened...
    CHECK(g_pausedFlags[g_pausedFlags.size() - 2]); // ...early enough that the exactly-1 check ran
}
