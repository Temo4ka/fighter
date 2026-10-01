#include "core/log.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>

namespace fighter::log {
namespace {

struct LoggerState {
    std::mutex Mutex;
    Level MinLevel = FIGHTER_DEBUG ? Level::Debug : Level::Info;
    std::ofstream File;
    std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();
};

LoggerState& getState() {
    static LoggerState State;
    return State;
}

std::string_view getLevelName(Level L) {
    switch (L) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?    ";
}

} // namespace

void setMinLevel(Level L) {
    std::scoped_lock Lock(getState().Mutex);
    getState().MinLevel = L;
}

Level getMinLevel() {
    return getState().MinLevel;
}

bool setFile(const std::filesystem::path& Path) {
    std::scoped_lock Lock(getState().Mutex);
    std::error_code Ec;
    if (Path.has_parent_path()) std::filesystem::create_directories(Path.parent_path(), Ec);
    getState().File = std::ofstream(Path, std::ios::out | std::ios::trunc);
    return getState().File.is_open();
}

void write(Level L, std::string_view Message) {
    LoggerState& State = getState();
    std::scoped_lock Lock(State.Mutex);

    const double Seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - State.Start).count();
    const std::string Line = std::format("[{:9.3f}] {} {}\n", Seconds, getLevelName(L), Message);

    std::fputs(Line.c_str(), stderr);
    if (State.File.is_open()) {
        State.File << Line;
        if (L >= Level::Warn) State.File.flush();
    }
}

} // namespace fighter::log
