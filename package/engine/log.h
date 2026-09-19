#pragma once
// Levelled logging to the console (stderr) and a log file.
//     LOG_I("input", "profile '%s' loaded: %d bindings", name.c_str(), n);
//     LOG_W / LOG_E / LOG_D likewise.  (printf-style; tag = your module's short name)
// Config (config/game.json): engine.log_level = debug | info | warn | error,  engine.log_file = "logs/game.log" ("" = no file).
#include <string>

namespace engine::log {

enum class Level { Debug = 0, Info = 1, Warn = 2, Error = 3 };

void setLevel(Level l);
bool setLevel(const std::string& name);       // "debug" | "info" | "warn" | "error"; false if unknown
Level level();
bool openFile(const std::string& path);       // truncates; creates the folder. Empty path closes the file.
void write(Level l, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

} // namespace engine::log

#define LOG_D(tag, ...) ::engine::log::write(::engine::log::Level::Debug, tag, __VA_ARGS__)
#define LOG_I(tag, ...) ::engine::log::write(::engine::log::Level::Info, tag, __VA_ARGS__)
#define LOG_W(tag, ...) ::engine::log::write(::engine::log::Level::Warn, tag, __VA_ARGS__)
#define LOG_E(tag, ...) ::engine::log::write(::engine::log::Level::Error, tag, __VA_ARGS__)
