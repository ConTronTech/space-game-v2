// Deliberately tiny: the engine finds every REGISTER_MODULE'd module by itself.
#include <cstdio>
#include <exception>
#include <filesystem>

#include "engine/engine.h"

// config/ and assets/ are found relative to the game folder. Move there so launching from another
// directory (a shortcut, an IDE, `../space_game_v2`) does not silently lose all bindings and assets.
static void enterGameFolder() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (ec) return;
    fs::path dir = exe.parent_path();
    if (fs::exists(dir / "config", ec)) fs::current_path(dir, ec);
}

int main(int argc, char** argv) {
    enterGameFolder();
    try {
        engine::Engine e;
        return e.run(argc, argv);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[fatal] uncaught exception: %s\n", ex.what());
    } catch (...) {
        std::fprintf(stderr, "[fatal] uncaught unknown exception\n");
    }
    return 2;
}
