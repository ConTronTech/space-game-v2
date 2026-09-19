#include "engine/log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <mutex>

namespace engine::log {

namespace {
struct State {
    Level level = Level::Info;
    std::FILE* file = nullptr;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::mutex mu;
    ~State() { if (file) std::fclose(file); }
};
State& st() { static State s; return s; }
const char* levelName(Level l) {
    switch (l) { case Level::Debug: return "DEBUG"; case Level::Info: return "INFO"; case Level::Warn: return "WARN"; default: return "ERROR"; }
}
} // namespace

void setLevel(Level l) { st().level = l; }
Level level() { return st().level; }

bool setLevel(const std::string& name) {
    if (name == "debug") setLevel(Level::Debug);
    else if (name == "info") setLevel(Level::Info);
    else if (name == "warn") setLevel(Level::Warn);
    else if (name == "error") setLevel(Level::Error);
    else return false;
    return true;
}

bool openFile(const std::string& path) {
    State& s = st();
    std::lock_guard<std::mutex> lock(s.mu);
    if (s.file) { std::fclose(s.file); s.file = nullptr; }
    if (path.empty()) return true;
    std::error_code ec;
    std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    s.file = std::fopen(path.c_str(), "w");
    return s.file != nullptr;
}

void write(Level l, const char* tag, const char* fmt, ...) {
    State& s = st();
    if (l < s.level) return;

    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    std::lock_guard<std::mutex> lock(s.mu);
    const char* prefix = l == Level::Warn ? "warning: " : l == Level::Error ? "error: " : l == Level::Debug ? "debug: " : "";
    std::fprintf(stderr, "[%s] %s%s\n", tag, prefix, msg);
    if (s.file) {
        double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.start).count();
        std::fprintf(s.file, "%9.3f %-5s [%s] %s\n", t, levelName(l), tag, msg);
        std::fflush(s.file); // keep the log useful after a crash
    }
}

} // namespace engine::log
