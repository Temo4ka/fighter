#include "core/log.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>

namespace fighter::log {
namespace {

struct State {
    std::mutex mutex;
    Level minLevel = FIGHTER_DEBUG ? Level::Debug : Level::Info;
    std::ofstream file;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

State& state() {
    static State s;
    return s;
}

std::string_view levelName(Level level) {
    switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?    ";
}

} // namespace

void setMinLevel(Level level) {
    std::scoped_lock lock(state().mutex);
    state().minLevel = level;
}

Level minLevel() {
    return state().minLevel;
}

bool setFile(const std::filesystem::path& path) {
    std::scoped_lock lock(state().mutex);
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    state().file = std::ofstream(path, std::ios::out | std::ios::trunc);
    return state().file.is_open();
}

void write(Level level, std::string_view message) {
    State& s = state();
    std::scoped_lock lock(s.mutex);

    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.start).count();
    const std::string line = std::format("[{:9.3f}] {} {}\n", t, levelName(level), message);

    std::fputs(line.c_str(), stderr);
    if (s.file.is_open()) {
        s.file << line;
        if (level >= Level::Warn) s.file.flush();
    }
}

} // namespace fighter::log
